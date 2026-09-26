/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "applib/app_message/app_message_internal.h"
#include "kernel/pbl_malloc.h"
#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/comm_session/session.h"
#include "pbl/services/comm_session/session_receive_router.h"
#include "pbl/services/app_inbox_service.h"
#include <pbl/logging/logging.h>
#include "pbl/util/math.h"

#include <stdint.h>

PBL_LOG_MODULE_DECLARE(service_app_message, CONFIG_SERVICE_APP_MESSAGE_LOG_LEVEL);

extern const ReceiverImplementation g_default_kernel_receiver_implementation;
extern const ReceiverImplementation g_app_message_receiver_implementation;

////////////////////////////////////////////////////////////////////////////////////////////////////
// ReceiverImplementation that writes App Message PP messages to the app's memory space using
// app_inbox_service. It also forwards a copy of the header to the default system receiver, but
// with a special handler that will always send a nack reply. If all goes well, this forward is
// cancelled in the end and the nack does not get sent.

//! The maximum amount of header bytes that is needed in order to let the system nack it.
//! To nack an App Message push, only the transaction ID is needed. Therefore, only buffer the
//! AppMessageHeader of the incoming push:
#define MAX_HEADER_SIZE (sizeof(AppMessageHeader))

#ifdef CONFIG_SOC_T5AI
//! Temporary metadata-only trace prefix: AppMessage header, UUID, dictionary count, and first
//! tuple header. Deliberately excludes tuple values.
#define T5AI_TRACE_PREFIX_SIZE \
  (APP_MSG_HDR_OVRHD_SIZE + sizeof(Dictionary) + offsetof(Tuple, value))
#endif

typedef struct {
  bool is_writing_to_app_inbox;

  CommSession *session;

  //! Used to keep track of how many header bytes are remaining to either forward to the default
  //! system receiver or to save them in the event the app inbox write fails in the end.
  //! We only want to write up to MAX_HEADER_SIZE, to keep the kernel heap impact to a minimum.
  size_t header_bytes_remaining;

#ifdef CONFIG_SOC_T5AI
  uint8_t trace_prefix[T5AI_TRACE_PREFIX_SIZE];
  size_t trace_prefix_length;
#endif

  //! Pointer to the default system receiver context, to which we want to forward the header data.
  Receiver *kernel_receiver;
} AppMessageReceiver;

static bool prv_fwd_prepare(AppMessageReceiver *rcv, CommSession *session,
                            size_t header_bytes_remaining) {
  // Try to set up a forward to the default system receiver that will send a nack back, based
  // on the header of the message:
  static const PebbleProtocolEndpoint kernel_nack_endpoint = {
    .endpoint_id = APP_MESSAGE_ENDPOINT_ID,
    .handler = app_message_app_protocol_system_nack_callback,
    .access_mask = PebbleProtocolAccessAny,
    .receiver_imp = &g_default_kernel_receiver_implementation,
    .receiver_opt = NULL,
  };
  Receiver *kernel_receiver = g_default_kernel_receiver_implementation.prepare(session,
                                           &kernel_nack_endpoint,
                                           header_bytes_remaining);
  if (!kernel_receiver) {
    PBL_LOG_ERR("System receiver wasn't able to prepare");
    return false;
  }
  rcv->kernel_receiver = kernel_receiver;
  return true;
}

static void prv_write(const uint8_t *data, size_t length) {
  app_inbox_service_write(AppInboxServiceTagAppMessageReceiver, data, length);
}

static Receiver *prv_app_message_receiver_prepare(CommSession *session,
                                                  const PebbleProtocolEndpoint *endpoint,
                                                  size_t total_payload_size) {
#ifdef CONFIG_SOC_T5AI
  PBL_LOG_INFO("T5AI pref trace: prepare payload=%"PRIu32,
               (uint32_t)total_payload_size);
#endif
  // FIXME: Find a better solution for this.
  // https://pebbletechnology.atlassian.net/browse/PBL-21538
  if (total_payload_size > 500) {
    comm_session_set_responsiveness(session, BtConsumerPpAppMessage, ResponseTimeMin,
                                    MIN_LATENCY_MODE_TIMEOUT_APP_MESSAGE_SECS);
  }

  AppMessageReceiver *rcv = (AppMessageReceiver *)kernel_zalloc(sizeof(AppMessageReceiver));
  if (!rcv) {
    return NULL;
  }
  rcv->session = session;

  const size_t header_bytes_remaining = MIN(MAX_HEADER_SIZE, total_payload_size);
  rcv->header_bytes_remaining = header_bytes_remaining;

  // Always forward the header to default system receiver as well, we'll cancel it later on if the
  // message was written succesfully to the app inbox.
  if (!prv_fwd_prepare(rcv, session, header_bytes_remaining)) {
    kernel_free(rcv);
    return NULL;
  }

  const size_t total_size = sizeof(AppMessageReceiverHeader) + total_payload_size;

  // Reasons why app_inbox_service_begin() might fail:
  // - the watchapp does not have App Message context opened
  // - there is no more space in the buffer that the app had allocated for it,
  // - the inbox is already being written to (by another CommSession) -- should be very rare
  if (app_inbox_service_begin(AppInboxServiceTagAppMessageReceiver, total_size, session)) {
    rcv->is_writing_to_app_inbox = true;

    // Log most recent communication timestamp
    const AppInstallId app_id = app_manager_get_current_app_id();
    app_install_mark_prioritized(app_id, true /* can_expire */);

    // Write the header, this info is needed for the app to handle the message and reply:
    const AppMessageReceiverHeader header = (const AppMessageReceiverHeader) {
      .session = session,
    };
    prv_write((const uint8_t *)&header, sizeof(header));
  }

  return (Receiver *)rcv;
}

static void prv_app_message_receiver_write(Receiver *receiver, const uint8_t *data, size_t length) {
  AppMessageReceiver *rcv = (AppMessageReceiver *)receiver;

#ifdef CONFIG_SOC_T5AI
  const size_t trace_bytes_to_copy = MIN(sizeof(rcv->trace_prefix) - rcv->trace_prefix_length,
                                         length);
  if (trace_bytes_to_copy) {
    memcpy(rcv->trace_prefix + rcv->trace_prefix_length, data, trace_bytes_to_copy);
    rcv->trace_prefix_length += trace_bytes_to_copy;
  }
#endif

  // FIXME: Find a better solution for this.
  // https://pebbletechnology.atlassian.net/browse/PBL-21538
  comm_session_set_responsiveness(rcv->session, BtConsumerPpAppMessage, ResponseTimeMin,
                                  MIN_LATENCY_MODE_TIMEOUT_APP_MESSAGE_SECS);

  if (rcv->header_bytes_remaining > 0) {
    const size_t header_bytes_to_write = MIN(rcv->header_bytes_remaining, length);
    g_default_kernel_receiver_implementation.write(rcv->kernel_receiver,
                                                   data, header_bytes_to_write);
    rcv->header_bytes_remaining -= header_bytes_to_write;
  }

  if (rcv->is_writing_to_app_inbox) {
    prv_write(data, length);
  }
}

static void prv_finally(AppMessageReceiver *receiver,
                        void (*kernel_receiver_finally_cb)(Receiver *)) {
  kernel_receiver_finally_cb(receiver->kernel_receiver);
  kernel_free(receiver);
}

static void prv_app_message_receiver_finish(Receiver *receiver) {
  AppMessageReceiver *rcv = (AppMessageReceiver *)receiver;

#ifdef CONFIG_SOC_T5AI
  if (rcv->trace_prefix_length >= sizeof(AppMessageHeader)) {
    const AppMessageHeader *header = (const AppMessageHeader *)rcv->trace_prefix;
    PBL_LOG_INFO("T5AI pref trace: finish cmd=%u txn=%u", header->command,
                 header->transaction_id);
    if ((header->command == CMD_PUSH) &&
        (rcv->trace_prefix_length >= T5AI_TRACE_PREFIX_SIZE)) {
      const AppMessagePush *push = (const AppMessagePush *)rcv->trace_prefix;
      const Tuple *tuple = push->dictionary.head;
      PBL_LOG_INFO("T5AI pref trace: dict count=%u first key=%"PRIu32" type=%u len=%u",
                   push->dictionary.count, tuple->key, tuple->type, tuple->length);
    }
  }
#endif

  // Default to letting the system receiver process the message and thus nack it:
  void (*kernel_receiver_finally_cb)(Receiver *) = g_default_kernel_receiver_implementation.finish;

  if (rcv->is_writing_to_app_inbox) {
    if (app_inbox_service_end(AppInboxServiceTagAppMessageReceiver)) {
#ifdef CONFIG_SOC_T5AI
      PBL_LOG_INFO("T5AI pref trace: queued for app");
#endif
      // The write was successful, cancel processing the header for nacking:
      kernel_receiver_finally_cb = g_default_kernel_receiver_implementation.cleanup;
      PBL_ANALYTICS_ADD(app_message_received_count, 1);
    }
#ifdef CONFIG_SOC_T5AI
    else {
      PBL_LOG_INFO("T5AI pref trace: app queue rejected message");
    }
#endif
  }

  prv_finally(rcv, kernel_receiver_finally_cb);
}

static void prv_app_message_receiver_cleanup(Receiver *receiver) {
  AppMessageReceiver *rcv = (AppMessageReceiver *)receiver;

  if (rcv->is_writing_to_app_inbox) {
    // Cancel the write, we don't want to deliver a broken message to the watchapp:
    app_inbox_service_cancel(AppInboxServiceTagAppMessageReceiver);
  }

  prv_finally(rcv, g_default_kernel_receiver_implementation.cleanup);
}

const ReceiverImplementation g_app_message_receiver_implementation = {
  .prepare = prv_app_message_receiver_prepare,
  .write = prv_app_message_receiver_write,
  .finish = prv_app_message_receiver_finish,
  .cleanup = prv_app_message_receiver_cleanup,
};
