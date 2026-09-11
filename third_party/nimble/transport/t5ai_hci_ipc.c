/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/mipc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define T5AI_MIPC_CPU_AP 1u
#define T5AI_MIPC_CPU_CP 0u
#define T5AI_MIPC_CONTROL_ACK 1u
#define T5AI_MIPC_SEND_FLAG_SYNC 1u
#define T5AI_HCI_TYPE 0u
#define T5AI_HCI_MAX_EVENT_SIZE 257u
#define T5AI_HCI_MAX_ACL_SIZE 1028u

enum {
  T5AI_HCI_INIT = 0,
  T5AI_HCI_DEINIT = 1,
  T5AI_HCI_RESET = 2,
  T5AI_HCI_COMMAND = 3,
  T5AI_HCI_ACL = 4,
  T5AI_HCI_EVENT_TO_HOST = 5,
  T5AI_HCI_ACL_TO_HOST = 6,
  T5AI_HCI_REGISTER_CALLBACKS = 7,
};

typedef struct {
  uint8_t type;
  uint8_t subtype;
  uint16_t result;
  uint32_t request;
  uint32_t request_length;
  uint32_t response;
  uint32_t response_length;
} T5AIHCIMessage;

typedef struct {
  T5AIMIPCChannelConfig config;
  T5AIMIPCHandle handle;
  T5AIMIPCData data __attribute__((aligned(32)));
  T5AIMIPCFrame frame __attribute__((aligned(32)));
} T5AIHCIOutbound;

static const char s_channel_name[] = "__tkl_ipc_handle";
static T5AIHCIOutbound s_request;
static T5AIHCIOutbound s_event_response;
static T5AIHCIMessage s_message __attribute__((aligned(32)));
static T5AIHCICallbacks s_callbacks;
static uint8_t s_sequence;
static bool s_started;
static bool s_request_transport_ack;
static bool s_event_response_transport_ack;
static T5AIMIPCFrame s_pending_frame __attribute__((aligned(32)));
static bool s_pending_frame_valid;
static uint8_t s_event_payload[T5AI_HCI_MAX_EVENT_SIZE] __attribute__((aligned(32)));
static uint8_t s_acl_payload[T5AI_HCI_MAX_ACL_SIZE] __attribute__((aligned(32)));

_Static_assert(sizeof(T5AIHCIMessage) == 20u, "T5AI HCI IPC ABI changed");

static bool prv_pointer(const void *pointer, uint32_t *wire_pointer) {
  const uintptr_t value = (uintptr_t)pointer;
  if (value == 0 || value > UINT32_MAX) {
    return false;
  }
  *wire_pointer = (uint32_t)value;
  return true;
}

static status_t prv_send_frame(T5AIMIPCFrame *frame) {
  dcache_flush(frame, sizeof(*frame));
  return t5ai_cp_mailbox_send(frame, sizeof(*frame));
}

static status_t prv_physical_ack(const T5AIMIPCFrame *received) {
  T5AIMIPCFrame ack = *received;
  ack.header |= (uint32_t)T5AI_MIPC_CONTROL_ACK << 12;
  return prv_send_frame(&ack);
}

static status_t prv_mipc_ack(T5AIMIPCFrame *received, T5AIMIPCData *remote_data) {
  remote_data->result = 0;
  dcache_flush(remote_data, sizeof(*remote_data));
  T5AIMIPCFrame ack = {
      .header = t5ai_mipc_frame_header(t5ai_mipc_header(T5AI_MIPC_CPU_AP, T5AI_MIPC_TYPE_ACK), 0, 0,
                                       ++s_sequence, T5AI_MIPC_TX_LOGICAL_CHANNEL),
      .object = received->object,
      .result = t5ai_mipc_data_crc(remote_data),
  };
  return prv_send_frame(&ack);
}

static status_t prv_prepare_outbound(T5AIHCIOutbound *outbound, const void *payload, size_t length,
                                     bool synchronous) {
  uint32_t config_pointer;
  uint32_t handle_pointer;
  uint32_t data_pointer;
  uint32_t payload_pointer;
  uint32_t name_pointer;
  if (length > UINT32_MAX || !prv_pointer(&outbound->config, &config_pointer) ||
      !prv_pointer(&outbound->handle, &handle_pointer) ||
      !prv_pointer(&outbound->data, &data_pointer) || !prv_pointer(payload, &payload_pointer) ||
      !prv_pointer(s_channel_name, &name_pointer)) {
    return E_RANGE;
  }

  outbound->config = (T5AIMIPCChannelConfig){.name = name_pointer};
  outbound->handle = (T5AIMIPCHandle){.config = config_pointer};
  outbound->data = (T5AIMIPCData){
      .handle = handle_pointer,
      .data = payload_pointer,
      .size = (uint32_t)length,
      .flags = synchronous ? T5AI_MIPC_SEND_FLAG_SYNC : 0,
  };
  outbound->frame = (T5AIMIPCFrame){
      .header = t5ai_mipc_frame_header(t5ai_mipc_header(T5AI_MIPC_CPU_AP, T5AI_MIPC_TYPE_COMMAND),
                                       0, 0, ++s_sequence, T5AI_MIPC_TX_LOGICAL_CHANNEL),
      .object = data_pointer,
      .result = t5ai_mipc_data_crc(&outbound->data),
  };
  dcache_flush(payload, length);
  dcache_flush(&outbound->config, sizeof(outbound->config));
  dcache_flush(&outbound->handle, sizeof(outbound->handle));
  dcache_flush(&outbound->data, sizeof(outbound->data));
  return prv_send_frame(&outbound->frame);
}

static status_t prv_read_frame(T5AIMIPCFrame *frame, bool blocking) {
  if (s_pending_frame_valid) {
    *frame = s_pending_frame;
    s_pending_frame_valid = false;
    return S_SUCCESS;
  }
  T5AICPMailboxFrame mailbox_frame;
  status_t status = blocking
                        ? t5ai_cp_mailbox_receive(T5AI_MIPC_RX_LOGICAL_CHANNEL, &mailbox_frame)
                        : t5ai_cp_mailbox_try_receive(T5AI_MIPC_RX_LOGICAL_CHANNEL, &mailbox_frame);
  if (FAILED(status) || mailbox_frame.length != sizeof(*frame)) {
    return status;
  }
  dcache_invalidate((void *)mailbox_frame.address, sizeof(*frame));
  memcpy(frame, (const void *)mailbox_frame.address, sizeof(*frame));
  return S_SUCCESS;
}

static bool prv_is_transport_ack(const T5AIMIPCFrame *frame, const T5AIMIPCData *data) {
  return (uint8_t)frame->header == t5ai_mipc_header(T5AI_MIPC_CPU_CP, T5AI_MIPC_TYPE_ACK) &&
         frame->object == (uint32_t)(uintptr_t)data;
}

static status_t prv_wait_for_event_response_ack(void) {
  while (s_event_response_transport_ack) {
    T5AIMIPCFrame received;
    status_t status = prv_read_frame(&received, true);
    if (FAILED(status)) {
      return status;
    }
    if ((received.header & (0xfu << 12)) != 0) {
      continue;
    }
    status = prv_physical_ack(&received);
    if (FAILED(status)) {
      return status;
    }
    if (prv_is_transport_ack(&received, &s_event_response.data)) {
      s_event_response_transport_ack = false;
    } else {
      s_pending_frame = received;
      s_pending_frame_valid = true;
      return E_BUSY;
    }
  }
  return S_SUCCESS;
}

static void prv_route_frame(uint8_t logical_channel, const T5AICPMailboxFrame *frame,
                            void *context) {
  (void)logical_channel;
  (void)context;
  if (s_pending_frame_valid || frame->length != sizeof(s_pending_frame)) {
    return;
  }
  dcache_invalidate((void *)frame->address, sizeof(s_pending_frame));
  memcpy(&s_pending_frame, (const void *)frame->address, sizeof(s_pending_frame));
  s_pending_frame_valid = true;
}

static status_t prv_decode_remote(T5AIMIPCFrame *frame, T5AIMIPCData **remote_data,
                                  T5AIHCIMessage **message) {
  if (frame->object == 0) {
    return E_ERROR;
  }
  *remote_data = (T5AIMIPCData *)(uintptr_t)frame->object;
  dcache_invalidate(*remote_data, sizeof(**remote_data));
  /* The pinned BK7258 MIPC receiver reports CRC mismatches but deliberately
   * continues dispatching the object. Mirror that ABI here: mutable list and
   * result fields can race the diagnostic CRC across CPUs. */
  if ((uint8_t)(frame->header >> 24) != T5AI_MIPC_RX_LOGICAL_CHANNEL || (*remote_data)->data == 0 ||
      (*remote_data)->size != sizeof(T5AIHCIMessage)) {
    return E_ERROR;
  }
  *message = (T5AIHCIMessage *)(uintptr_t)(*remote_data)->data;
  dcache_invalidate(*message, sizeof(**message));
  return (*message)->type == T5AI_HCI_TYPE ? S_SUCCESS : E_ERROR;
}

static status_t prv_respond_to_event(T5AIHCIMessage *message) {
  /* This mirrors tuya_ipc_send_no_sync() in tkl_hci_1.c.  The CP is blocked in
   * tuya_ipc_send_sync() waiting for this echoed message. Retire the preceding
   * asynchronous response before reusing its shared descriptor, then enqueue
   * this response without waiting for its ACK in the callback path. */
  status_t status = prv_wait_for_event_response_ack();
  if (FAILED(status)) {
    return status;
  }
  status = prv_prepare_outbound(&s_event_response, message, sizeof(*message), false);
  if (PASSED(status)) {
    s_event_response_transport_ack = true;
  }
  return status;
}

static status_t prv_handle_command(T5AIMIPCFrame *frame, uint8_t expected, bool *matched) {
  T5AIMIPCData *remote_data;
  T5AIHCIMessage *message;
  status_t status = prv_decode_remote(frame, &remote_data, &message);
  if (FAILED(status)) {
    return status;
  }

  if (message->subtype == T5AI_HCI_EVENT_TO_HOST || message->subtype == T5AI_HCI_ACL_TO_HOST) {
    uint8_t *payload = message->subtype == T5AI_HCI_EVENT_TO_HOST ? s_event_payload : s_acl_payload;
    const size_t capacity = message->subtype == T5AI_HCI_EVENT_TO_HOST ? sizeof(s_event_payload)
                                                                       : sizeof(s_acl_payload);
    if (message->request == 0 || message->request_length > capacity) {
      return E_ERROR;
    }
    dcache_invalidate((void *)(uintptr_t)message->request, message->request_length);
    memcpy(payload, (const void *)(uintptr_t)message->request, message->request_length);
    status = prv_respond_to_event(message);
    if (PASSED(status)) {
      T5AIHCIReceiveCallback callback =
          message->subtype == T5AI_HCI_EVENT_TO_HOST ? s_callbacks.event : s_callbacks.acl;
      if (callback != NULL) {
        callback(payload, message->request_length);
      }
    }
  } else if (message->subtype == expected) {
    s_message.result = message->result;
    *matched = true;
  }
  status_t ack_status = prv_mipc_ack(frame, remote_data);
  return FAILED(status) ? status : ack_status;
}

static status_t prv_exchange(uint8_t subtype, const void *payload, size_t length) {
  status_t status = prv_wait_for_event_response_ack();
  if (FAILED(status)) {
    return status;
  }
  s_message = (T5AIHCIMessage){.type = T5AI_HCI_TYPE, .subtype = subtype};
  if (payload != NULL && (!prv_pointer(payload, &s_message.request) || length > UINT32_MAX)) {
    return E_RANGE;
  }
  s_message.request_length = (uint32_t)length;
  status = prv_prepare_outbound(&s_request, &s_message, sizeof(s_message), false);
  if (FAILED(status)) {
    return status;
  }

  s_request_transport_ack = false;
  bool matched = false;
  while (!s_request_transport_ack || !matched) {
    T5AIMIPCFrame received;
    status = prv_read_frame(&received, true);
    if (FAILED(status)) {
      return status;
    }
    if ((received.header & (0xfu << 12)) != 0) {
      continue;
    }
    status = prv_physical_ack(&received);
    if (FAILED(status)) {
      return status;
    }
    const uint8_t command = (uint8_t)received.header;
    if (prv_is_transport_ack(&received, &s_request.data)) {
      s_request_transport_ack = true;
    } else if (prv_is_transport_ack(&received, &s_event_response.data)) {
      s_event_response_transport_ack = false;
    } else if (command == t5ai_mipc_header(T5AI_MIPC_CPU_CP, T5AI_MIPC_TYPE_COMMAND)) {
      status = prv_handle_command(&received, subtype, &matched);
      if (FAILED(status)) {
        return status;
      }
    }
  }
  return s_message.result == 0 ? S_SUCCESS : E_ERROR;
}

static status_t prv_locked_exchange(uint8_t subtype, const void *payload, size_t length) {
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  status = prv_exchange(subtype, payload, length);
  t5ai_cp_mailbox_unlock();
  return status;
}

int t5ai_hci_ipc_start(const T5AIHCICallbacks *callbacks) {
  if (callbacks == NULL || callbacks->event == NULL || callbacks->acl == NULL || s_started) {
    return E_INVALID_ARGUMENT;
  }
  s_callbacks = *callbacks;
  s_pending_frame_valid = false;
  s_event_response_transport_ack = false;
  status_t status = t5ai_cp_mailbox_register(T5AI_MIPC_RX_LOGICAL_CHANNEL, prv_route_frame, NULL);
  if (FAILED(status)) {
    memset(&s_callbacks, 0, sizeof(s_callbacks));
    return status;
  }
  status = prv_locked_exchange(T5AI_HCI_REGISTER_CALLBACKS, NULL, 0);
  if (PASSED(status)) {
    status = prv_locked_exchange(T5AI_HCI_INIT, NULL, 0);
  }
  s_started = PASSED(status);
  if (!s_started) {
    t5ai_cp_mailbox_unregister(T5AI_MIPC_RX_LOGICAL_CHANNEL);
    memset(&s_callbacks, 0, sizeof(s_callbacks));
  }
  return status;
}

int t5ai_hci_ipc_stop(void) {
  if (!s_started) {
    return E_INVALID_OPERATION;
  }
  status_t status = prv_locked_exchange(T5AI_HCI_DEINIT, NULL, 0);
  s_started = false;
  s_pending_frame_valid = false;
  s_event_response_transport_ack = false;
  t5ai_cp_mailbox_unregister(T5AI_MIPC_RX_LOGICAL_CHANNEL);
  memset(&s_callbacks, 0, sizeof(s_callbacks));
  return status;
}

int t5ai_hci_ipc_send_command(const uint8_t *data, size_t length) {
  return !s_started || data == NULL ? E_INVALID_OPERATION
                                    : prv_locked_exchange(T5AI_HCI_COMMAND, data, length);
}

int t5ai_hci_ipc_send_acl(const uint8_t *data, size_t length) {
  return !s_started || data == NULL ? E_INVALID_OPERATION
                                    : prv_locked_exchange(T5AI_HCI_ACL, data, length);
}

void t5ai_hci_ipc_poll(void) {
  if (!s_started || FAILED(t5ai_cp_mailbox_lock())) {
    return;
  }
  T5AIMIPCFrame received;
  if (PASSED(prv_read_frame(&received, false)) && (received.header & (0xfu << 12)) == 0) {
    (void)prv_physical_ack(&received);
    if (prv_is_transport_ack(&received, &s_event_response.data)) {
      s_event_response_transport_ack = false;
    } else if ((uint8_t)received.header ==
               t5ai_mipc_header(T5AI_MIPC_CPU_CP, T5AI_MIPC_TYPE_COMMAND)) {
      bool ignored = false;
      (void)prv_handle_command(&received, UINT8_MAX, &ignored);
    }
  }
  t5ai_cp_mailbox_unlock();
}
