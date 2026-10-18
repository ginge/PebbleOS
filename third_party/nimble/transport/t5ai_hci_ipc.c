/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/mcu/cache.h>
#include <pbl/logging/logging.h>
PBL_LOG_MODULE_DEFINE(t5ai_hci_ipc, LOG_LEVEL_DEBUG);
#include <pbl/kernel/kernel.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/mipc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


// Shared objects must start on a BK7258 data-cache line.
#define T5AI_HCI_MESSAGE_WIRE_SIZE 20u
#define T5AI_HCI_WORKER_PRIORITY (CONFIG_KERNEL_NUM_PRIORITIES - 2u)
// Bounded waits for the persistent worker to drain frames and retire CP ownership.
#define T5AI_HCI_RECOVERY_DRAIN_TIMEOUT_MS 50u
#define T5AI_HCI_STOP_RETIRE_TIMEOUT_MS 250u
#define T5AI_HCI_BUSY_RETRY_DELAY_MS 1u
#define T5AI_HCI_OWNERSHIP_POLL_INTERVAL_MS 1u
#define T5AI_HCI_NO_EXPECTED_SUBTYPE UINT8_MAX

#define T5AI_MIPC_CPU_AP 1u
#define T5AI_MIPC_CPU_CP 0u
#define T5AI_MIPC_SEND_FLAG_SYNC 1u
#define T5AI_HCI_TYPE 0u
#define T5AI_SYSTEM_TYPE 1u
#define T5AI_SYSTEM_REBOOT 0u
// Buffer capacities include the HCI event/ACL header, matching the CP transport.
#define T5AI_HCI_MAX_EVENT_SIZE (2u + UINT8_MAX)
#define T5AI_HCI_MAX_ACL_SIZE (4u + 1024u)
#define T5AI_HCI_PENDING_FRAME_CAPACITY 8u
#define T5AI_HCI_EVENT_RESPONSE_CAPACITY 8u
#define T5AI_HCI_REQUEST_CAPACITY 8u
#define T5AI_HCI_WORKER_STACK_SIZE 3072u
#define T5AI_HCI_SEND_TIMEOUT_MS 2000u

// Subtype values are fixed by the pinned Tuya CP IPC ABI.
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
  T5AIMIPCData data __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
  T5AIMIPCFrame frame __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
  T5AIHCIMessage message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
} T5AIHCIOutbound;

typedef struct {
  T5AIMIPCFrame frame;
} T5AIHCIPendingFrame;

static const char s_channel_name[] = "__tkl_ipc_handle";
static T5AIHCIOutbound s_requests[T5AI_HCI_REQUEST_CAPACITY];
static bool s_request_in_use[T5AI_HCI_REQUEST_CAPACITY];
static T5AIHCIOutbound s_event_responses[T5AI_HCI_EVENT_RESPONSE_CAPACITY];
static T5AIHCIOutbound s_system_reboot_request;
static bool s_event_response_in_use[T5AI_HCI_EVENT_RESPONSE_CAPACITY];
static T5AIHCICallbacks s_callbacks;
static uint8_t s_sequence;
static uint16_t s_exchange_result;
static bool s_started;
static bool s_faulted;
static bool s_accept_frames;
static PBL_MUTEX_DEFINE(s_sync_mutex);
static PBL_MUTEX_DEFINE(s_async_mutex);
static PBL_SEM_DEFINE(s_worker_wake, 0, 1);
static PBL_SEM_DEFINE(s_exchange_ready, 0, 1);
static struct pbl_thread s_worker_task;
PBL_THREAD_STACK_DEFINE(s_worker_stack, T5AI_HCI_WORKER_STACK_SIZE)
    __attribute__((section(".t5ai_board_cache")));
static bool s_worker_created;
static volatile uint8_t s_expected_subtype = T5AI_HCI_NO_EXPECTED_SUBTYPE;
static T5AIHCIPendingFrame s_pending_frames[T5AI_HCI_PENDING_FRAME_CAPACITY]
    __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint8_t s_pending_frame_head;
static uint8_t s_pending_frame_count;
static uint8_t s_event_payload[T5AI_HCI_MAX_EVENT_SIZE]
    __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint8_t s_acl_payload[T5AI_HCI_MAX_ACL_SIZE] __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
#ifdef CONFIG_T5AI_HCI_PROFILE
static T5AIHCIIpcDiagnostics s_diagnostics;
#endif

_Static_assert(sizeof(T5AIHCIMessage) == T5AI_HCI_MESSAGE_WIRE_SIZE, "T5AI HCI IPC ABI changed");

static bool prv_started(void) {
  return __atomic_load_n(&s_started, __ATOMIC_ACQUIRE);
}

static bool prv_faulted(void) {
  return __atomic_load_n(&s_faulted, __ATOMIC_ACQUIRE);
}

static void prv_set_started(bool started) {
  __atomic_store_n(&s_started, started, __ATOMIC_RELEASE);
}

static void prv_mark_faulted(void) {
  __atomic_store_n(&s_faulted, true, __ATOMIC_RELEASE);
  prv_set_started(false);
}

static uint8_t prv_next_sequence(void) {
  return __atomic_add_fetch(&s_sequence, 1u, __ATOMIC_RELAXED);
}

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
  const pbl_tick_t start = pbl_uptime_ticks();
  status_t status;
  do {
    status = t5ai_cp_mailbox_send(frame, sizeof(*frame));
    if (status != E_BUSY) {
      return status;
    }
#ifdef CONFIG_T5AI_HCI_PROFILE
    ++s_diagnostics.send_busy_retries;
#endif
    pbl_thread_sleep(PBL_MSEC(T5AI_HCI_BUSY_RETRY_DELAY_MS));
  } while (pbl_uptime_ticks() - start < pbl_ms_to_ticks(T5AI_HCI_SEND_TIMEOUT_MS));
#ifdef CONFIG_T5AI_HCI_PROFILE
  ++s_diagnostics.send_timeouts;
#endif
  PBL_LOG_ERR("T5AI HCI MIPC send timed out: header=%08lx object=%08lx",
                (unsigned long)frame->header, (unsigned long)frame->object);
  return E_AGAIN;
}

static status_t prv_mipc_ack(T5AIMIPCFrame *received, T5AIMIPCData *remote_data) {
  remote_data->result = 0;
  dcache_flush(remote_data, sizeof(*remote_data));
  T5AIMIPCFrame ack = {
      .header = t5ai_mipc_frame_header(t5ai_mipc_header(T5AI_MIPC_CPU_AP, T5AI_MIPC_TYPE_ACK), 0, 0,
                                       prv_next_sequence(), T5AI_MIPC_TX_LOGICAL_CHANNEL),
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
                                       0, 0, prv_next_sequence(), T5AI_MIPC_TX_LOGICAL_CHANNEL),
      .object = data_pointer,
      .result = t5ai_mipc_data_crc(&outbound->data),
  };
  dcache_flush(payload, length);
  dcache_flush(&outbound->config, sizeof(outbound->config));
  dcache_flush(&outbound->handle, sizeof(outbound->handle));
  dcache_flush(&outbound->data, sizeof(outbound->data));
  return prv_send_frame(&outbound->frame);
}

static bool prv_is_transport_ack(const T5AIMIPCFrame *frame, T5AIMIPCData *data) {
  if ((uint8_t)frame->header != t5ai_mipc_header(T5AI_MIPC_CPU_CP, T5AI_MIPC_TYPE_ACK) ||
      frame->object != (uint32_t)(uintptr_t)data) {
    return false;
  }
  /* The vendor MIPC implementation mutates descriptor state before returning
   * an ACK and treats a CRC mismatch as diagnostic.  It is therefore not a
   * stable transaction-generation token; ownership completion is identified
   * by the ACK type and the still-in-use descriptor address. */
  dcache_invalidate(data, sizeof(*data));
  (void)t5ai_mipc_frame_valid(frame, data);
  return true;
}

static status_t prv_queue_pending_frame(const T5AIMIPCFrame *frame) {
  pbl_irq_lock();
  if (s_pending_frame_count == T5AI_HCI_PENDING_FRAME_CAPACITY) {
#ifdef CONFIG_T5AI_HCI_PROFILE
    ++s_diagnostics.pending_queue_exhaustions;
#endif
    pbl_irq_unlock();
    return E_OUT_OF_RESOURCES;
  }
  const uint8_t tail =
      (uint8_t)((s_pending_frame_head + s_pending_frame_count) % T5AI_HCI_PENDING_FRAME_CAPACITY);
  s_pending_frames[tail] = (T5AIHCIPendingFrame){.frame = *frame};
  ++s_pending_frame_count;
#ifdef CONFIG_T5AI_HCI_PROFILE
  if (s_pending_frame_count > s_diagnostics.pending_queue_high_water) {
    s_diagnostics.pending_queue_high_water = s_pending_frame_count;
  }
#endif
  pbl_irq_unlock();
  return S_SUCCESS;
}

static bool prv_retire_event_response(const T5AIMIPCFrame *frame) {
  bool retired = false;
  pbl_irq_lock();
  for (size_t i = 0; i < T5AI_HCI_EVENT_RESPONSE_CAPACITY; ++i) {
    if (s_event_response_in_use[i] &&
        prv_is_transport_ack(frame, &s_event_responses[i].data)) {
      s_event_response_in_use[i] = false;
      retired = true;
      break;
    }
  }
  pbl_irq_unlock();
  return retired;
}

static bool prv_retire_request(const T5AIMIPCFrame *frame) {
  bool retired = false;
  pbl_irq_lock();
  for (size_t i = 0; i < T5AI_HCI_REQUEST_CAPACITY; ++i) {
    if (s_request_in_use[i] && prv_is_transport_ack(frame, &s_requests[i].data)) {
      s_request_in_use[i] = false;
      retired = true;
      break;
    }
  }
  pbl_irq_unlock();
  return retired;
}

static T5AIHCIOutbound *prv_allocate_request(void) {
  T5AIHCIOutbound *request = NULL;
  pbl_irq_lock();
  for (size_t i = 0; i < T5AI_HCI_REQUEST_CAPACITY; ++i) {
    if (!s_request_in_use[i]) {
      s_request_in_use[i] = true;
      request = &s_requests[i];
      break;
    }
  }
  pbl_irq_unlock();
  return request;
}

static bool prv_all_outbound_retired(void) {
  bool retired = true;
  pbl_irq_lock();
  for (size_t i = 0; i < T5AI_HCI_REQUEST_CAPACITY; ++i) {
    retired = retired && !s_request_in_use[i];
  }
  for (size_t i = 0; i < T5AI_HCI_EVENT_RESPONSE_CAPACITY; ++i) {
    retired = retired && !s_event_response_in_use[i];
  }
  pbl_irq_unlock();
  return retired;
}

static bool prv_pending_empty(void) {
  pbl_irq_lock();
  const bool empty = s_pending_frame_count == 0u;
  pbl_irq_unlock();
  return empty;
}

static status_t prv_handle_command(T5AIMIPCFrame *frame, uint8_t expected, bool *matched);

static bool prv_dequeue_pending_frame(T5AIMIPCFrame *frame) {
  bool available = false;
  pbl_irq_lock();
  if (s_pending_frame_count != 0u) {
    *frame = s_pending_frames[s_pending_frame_head].frame;
    s_pending_frame_head = (uint8_t)((s_pending_frame_head + 1u) % T5AI_HCI_PENDING_FRAME_CAPACITY);
    --s_pending_frame_count;
    available = true;
  }
  pbl_irq_unlock();
  return available;
}

static void prv_worker(void *context) {
  (void)context;
  while (true) {
    (void)pbl_sem_take(&s_worker_wake, PBL_TICKS(PBL_TICK_FOREVER));
    T5AIMIPCFrame frame;
    while (prv_dequeue_pending_frame(&frame)) {
      if (prv_retire_request(&frame) || prv_retire_event_response(&frame)) {
#ifdef CONFIG_T5AI_HCI_PROFILE
        ++s_diagnostics.transport_acks_received;
#endif
        continue;
      }
      if ((uint8_t)frame.header !=
          t5ai_mipc_header(T5AI_MIPC_CPU_CP, T5AI_MIPC_TYPE_COMMAND)) {
        continue;
      }
      pbl_mutex_lock(&s_async_mutex, PBL_FOREVER);
      bool matched = false;
      const status_t status = prv_handle_command(
          &frame, __atomic_load_n(&s_expected_subtype, __ATOMIC_ACQUIRE), &matched);
      pbl_mutex_unlock(&s_async_mutex);
      if (FAILED(status)) {
        PBL_LOG_ERR("T5AI HCI command handling failed: status=%ld header=%08lx expected=%u",
                      (long)status, (unsigned long)frame.header,
                      (unsigned)__atomic_load_n(&s_expected_subtype, __ATOMIC_ACQUIRE));
        prv_mark_faulted();
      }
      if (matched) {
        (void)pbl_sem_give(&s_exchange_ready);
      }
    }
  }
}

static void prv_route_frame(uint8_t logical_channel, const T5AICPMailboxFrame *frame,
                            void *context) {
  (void)context;
  if (!__atomic_load_n(&s_accept_frames, __ATOMIC_ACQUIRE) ||
      logical_channel != T5AI_MIPC_RX_LOGICAL_CHANNEL ||
      frame->length != sizeof(T5AIMIPCFrame)) {
    (void)t5ai_cp_mailbox_acknowledge_frame(frame, false);
    return;
  }
  T5AIMIPCFrame pending;
#ifdef CONFIG_T5AI_HCI_PROFILE
  ++s_diagnostics.frames_received;
#endif
  memcpy(&pending, (const void *)frame->address, sizeof(pending));
  (void)t5ai_cp_mailbox_acknowledge_frame(frame, true);
  if (FAILED(prv_queue_pending_frame(&pending))) {
    PBL_LOG_ERR("T5AI HCI pending frame queue exhausted");
    prv_mark_faulted();
  } else {
    (void)pbl_sem_give(&s_worker_wake);
  }
}

static status_t prv_decode_remote(T5AIMIPCFrame *frame, T5AIMIPCData **remote_data,
                                  T5AIHCIMessage **message) {
  if (!t5ai_cp_mailbox_shared_range_valid((uintptr_t)frame->object,
                                           sizeof(T5AIMIPCData))) {
    return E_ERROR;
  }
  *remote_data = (T5AIMIPCData *)(uintptr_t)frame->object;
  dcache_invalidate(*remote_data, sizeof(**remote_data));
  /* The pinned BK7258 MIPC receiver reports CRC mismatches but deliberately
   * continues dispatching the object. Mirror that ABI here: mutable list and
   * result fields can race the diagnostic CRC across CPUs. */
  if ((*remote_data)->size != sizeof(T5AIHCIMessage) ||
      !t5ai_cp_mailbox_shared_range_valid((uintptr_t)(*remote_data)->data,
                                           sizeof(T5AIHCIMessage))) {
    return E_ERROR;
  }
  *message = (T5AIHCIMessage *)(uintptr_t)(*remote_data)->data;
  dcache_invalidate(*message, sizeof(**message));
  return (*message)->type == T5AI_HCI_TYPE ? S_SUCCESS : E_ERROR;
}

static status_t prv_respond_to_event(T5AIHCIMessage *message) {
  status_t result = E_OUT_OF_RESOURCES;
  pbl_irq_lock();
  for (size_t i = 0; i < T5AI_HCI_EVENT_RESPONSE_CAPACITY; ++i) {
    if (!s_event_response_in_use[i]) {
      s_event_response_in_use[i] = true;
      pbl_irq_unlock();
      result = prv_prepare_outbound(&s_event_responses[i], message, sizeof(*message), false);
      if (FAILED(result)) {
        pbl_irq_lock();
        s_event_response_in_use[i] = false;
        pbl_irq_unlock();
      }
      if (PASSED(result)) {
#ifdef CONFIG_T5AI_HCI_PROFILE
        ++s_diagnostics.responses_sent;
#endif
      }
      return result;
    }
  }
  pbl_irq_unlock();
#ifdef CONFIG_T5AI_HCI_PROFILE
  ++s_diagnostics.response_exhaustions;
#endif
  PBL_LOG_ERR("T5AI HCI event response slots exhausted");
  return result;
}

static status_t prv_handle_command(T5AIMIPCFrame *frame, uint8_t expected, bool *matched) {
  T5AIMIPCData *remote_data;
  T5AIHCIMessage *message;
  status_t status = prv_decode_remote(frame, &remote_data, &message);
  if (FAILED(status)) {
    return status;
  }

  if (message->subtype == T5AI_HCI_EVENT_TO_HOST || message->subtype == T5AI_HCI_ACL_TO_HOST) {
    /* CP owns and reuses message as soon as prv_respond_to_event() publishes
     * our reply. Snapshot every field needed by the callback before replying;
     * rereading the remote descriptor afterwards races that reuse. */
    const uint8_t subtype = message->subtype;
    if (subtype == T5AI_HCI_EVENT_TO_HOST) {
#ifdef CONFIG_T5AI_HCI_PROFILE
      ++s_diagnostics.events_received;
#endif
    } else {
#ifdef CONFIG_T5AI_HCI_PROFILE
      ++s_diagnostics.acl_received;
#endif
    }
    const uint32_t request = message->request;
    const size_t request_length = message->request_length;
    uint8_t *payload = subtype == T5AI_HCI_EVENT_TO_HOST ? s_event_payload : s_acl_payload;
    const size_t capacity = subtype == T5AI_HCI_EVENT_TO_HOST ? sizeof(s_event_payload)
                                                              : sizeof(s_acl_payload);
    if (request_length > capacity ||
        !t5ai_cp_mailbox_shared_range_valid((uintptr_t)request, request_length)) {
      return E_ERROR;
    }
    dcache_invalidate((void *)(uintptr_t)request, request_length);
    memcpy(payload, (const void *)(uintptr_t)request, request_length);
    status = prv_respond_to_event(message);
    if (PASSED(status)) {
      T5AIHCIReceiveCallback callback =
          subtype == T5AI_HCI_EVENT_TO_HOST ? s_callbacks.event : s_callbacks.acl;
      if (callback != NULL) {
        callback(payload, request_length);
      }
    }
  } else if (message->subtype == expected) {
#ifdef CONFIG_T5AI_HCI_PROFILE
    ++s_diagnostics.commands_received;
#endif
    s_exchange_result = message->result;
    *matched = true;
  }
  status_t ack_status = prv_mipc_ack(frame, remote_data);
  if (PASSED(ack_status)) {
#ifdef CONFIG_T5AI_HCI_PROFILE
    ++s_diagnostics.mipc_acks_sent;
#endif
  }
  return FAILED(status) ? status : ack_status;
}

static status_t prv_exchange(uint8_t subtype, const void *payload, size_t length) {
  T5AIHCIOutbound *request = prv_allocate_request();
  if (request == NULL) {
#ifdef CONFIG_T5AI_HCI_PROFILE
    ++s_diagnostics.request_exhaustions;
#endif
    PBL_LOG_ERR("T5AI HCI request slots exhausted: subtype=%u", subtype);
    prv_mark_faulted();
    return E_OUT_OF_RESOURCES;
  }
  request->message = (T5AIHCIMessage){.type = T5AI_HCI_TYPE, .subtype = subtype};
  if (payload != NULL && (!prv_pointer(payload, &request->message.request) || length > UINT32_MAX)) {
    s_request_in_use[request - s_requests] = false;
    return E_RANGE;
  }
  request->message.request_length = (uint32_t)length;
  // The IPC descriptor contains a pointer rather than an inline HCI packet.
  // Publish the pointed-to bytes before CPU0 dereferences that pointer.
  if (payload != NULL && length != 0u) {
    dcache_flush(payload, length);
  }
  while (pbl_sem_take(&s_exchange_ready, PBL_TICKS(0u)) == 0) {
  }
  __atomic_store_n(&s_expected_subtype, subtype, __ATOMIC_RELEASE);
  status_t status =
      prv_prepare_outbound(request, &request->message, sizeof(request->message), false);
  if (FAILED(status)) {
    __atomic_store_n(&s_expected_subtype, T5AI_HCI_NO_EXPECTED_SUBTYPE, __ATOMIC_RELEASE);
    s_request_in_use[request - s_requests] = false;
    return status;
  }

  if (pbl_sem_take(&s_exchange_ready, PBL_TICKS(pbl_ms_to_ticks(T5AI_HCI_SEND_TIMEOUT_MS))) != 0) {
#ifdef CONFIG_T5AI_HCI_PROFILE
    ++s_diagnostics.exchange_timeouts;
#endif
    PBL_LOG_ERR("T5AI HCI exchange timed out: subtype=%u", subtype);
    __atomic_store_n(&s_expected_subtype, T5AI_HCI_NO_EXPECTED_SUBTYPE, __ATOMIC_RELEASE);
    prv_mark_faulted();
    return E_AGAIN;
  }
  __atomic_store_n(&s_expected_subtype, T5AI_HCI_NO_EXPECTED_SUBTYPE, __ATOMIC_RELEASE);
  return s_exchange_result == 0 ? S_SUCCESS : E_ERROR;
}

static status_t prv_locked_exchange(uint8_t subtype, const void *payload, size_t length) {
  pbl_mutex_lock(&s_sync_mutex, PBL_FOREVER);
  status_t status = prv_exchange(subtype, payload, length);
  pbl_mutex_unlock(&s_sync_mutex);
  return status;
}

int t5ai_hci_ipc_start(const T5AIHCICallbacks *callbacks) {
  if (callbacks == NULL || callbacks->event == NULL || callbacks->acl == NULL || prv_started()) {
    return E_INVALID_ARGUMENT;
  }
  const bool recovering = prv_faulted();
  s_callbacks = *callbacks;
  if (!s_worker_created) {
    const struct pbl_thread_attr attr = {
      .name = "T5AI HCI IPC",
      .entry = prv_worker,
      .prio = T5AI_HCI_WORKER_PRIORITY,
      .privileged = true,
      .stack = s_worker_stack,
      .stack_size = sizeof(s_worker_stack),
    };
    if (pbl_thread_create(&s_worker_task, &attr) != 0) {
      memset(&s_callbacks, 0, sizeof(s_callbacks));
      return E_OUT_OF_MEMORY;
    }
    s_worker_created = true;
  }
  if (recovering) {
    /* Let the persistent worker consume already-snapshotted ACKs before the
     * route is replaced. Timed-out descriptor slots remain allocated: a late
     * transport ACK may retire them, but no restart may reuse their address. */
    (void)pbl_sem_give(&s_worker_wake);
    const pbl_tick_t drain_start = pbl_uptime_ticks();
    while (!prv_pending_empty() &&
           pbl_uptime_ticks() - drain_start < pbl_ms_to_ticks(T5AI_HCI_RECOVERY_DRAIN_TIMEOUT_MS)) {
      pbl_thread_sleep(PBL_MSEC(T5AI_HCI_OWNERSHIP_POLL_INTERVAL_MS));
    }
    __atomic_store_n(&s_accept_frames, false, __ATOMIC_RELEASE);
    t5ai_cp_mailbox_unregister(T5AI_MIPC_RX_LOGICAL_CHANNEL);
    pbl_mutex_lock(&s_async_mutex, PBL_FOREVER);
    s_pending_frame_head = 0u;
    s_pending_frame_count = 0u;
    pbl_mutex_unlock(&s_async_mutex);
  } else {
    s_pending_frame_head = 0u;
    s_pending_frame_count = 0u;
    memset(s_request_in_use, 0, sizeof(s_request_in_use));
    memset(s_event_response_in_use, 0, sizeof(s_event_response_in_use));
  }
  status_t status = t5ai_cp_mailbox_register(T5AI_MIPC_RX_LOGICAL_CHANNEL, prv_route_frame, NULL);
  if (FAILED(status)) {
    memset(&s_callbacks, 0, sizeof(s_callbacks));
    return status;
  }
  __atomic_store_n(&s_accept_frames, true, __ATOMIC_RELEASE);
  /* Reset first: Tuya's reset path deinitializes and reinitializes Bluetooth,
   * which clears the controller's HCI receive callbacks. */
  status = prv_locked_exchange(T5AI_HCI_RESET, NULL, 0);
  if (PASSED(status)) {
    status = prv_locked_exchange(T5AI_HCI_REGISTER_CALLBACKS, NULL, 0);
  }
  if (PASSED(status)) {
    __atomic_store_n(&s_faulted, false, __ATOMIC_RELEASE);
  }
  prv_set_started(PASSED(status));
  if (!prv_started()) {
    __atomic_store_n(&s_accept_frames, false, __ATOMIC_RELEASE);
    t5ai_cp_mailbox_unregister(T5AI_MIPC_RX_LOGICAL_CHANNEL);
    memset(&s_callbacks, 0, sizeof(s_callbacks));
  }
  return status;
}

int t5ai_hci_ipc_stop(void) {
  if (!prv_started()) {
    return E_INVALID_OPERATION;
  }
  status_t status = prv_locked_exchange(T5AI_HCI_DEINIT, NULL, 0);
  prv_set_started(false);
  if (FAILED(status)) {
    prv_mark_faulted();
    return status;
  }
  const pbl_tick_t retirement_start = pbl_uptime_ticks();
  while (!prv_all_outbound_retired() &&
         pbl_uptime_ticks() - retirement_start < pbl_ms_to_ticks(T5AI_HCI_STOP_RETIRE_TIMEOUT_MS)) {
    (void)pbl_sem_give(&s_worker_wake);
    pbl_thread_sleep(PBL_MSEC(T5AI_HCI_OWNERSHIP_POLL_INTERVAL_MS));
  }
  if (!prv_all_outbound_retired()) {
    PBL_LOG_ERR("T5AI HCI stop left CP-owned descriptors quarantined");
    prv_mark_faulted();
    return E_AGAIN;
  }
  __atomic_store_n(&s_accept_frames, false, __ATOMIC_RELEASE);
  t5ai_cp_mailbox_unregister(T5AI_MIPC_RX_LOGICAL_CHANNEL);
  pbl_mutex_lock(&s_async_mutex, PBL_FOREVER);
  s_pending_frame_head = 0u;
  s_pending_frame_count = 0u;
  memset(s_request_in_use, 0, sizeof(s_request_in_use));
  memset(s_event_response_in_use, 0, sizeof(s_event_response_in_use));
  memset(&s_callbacks, 0, sizeof(s_callbacks));
  pbl_mutex_unlock(&s_async_mutex);
  return status;
}

int t5ai_hci_ipc_send_command(const uint8_t *data, size_t length) {
  return !prv_started() || prv_faulted() || data == NULL ? E_INVALID_OPERATION
                                    : prv_locked_exchange(T5AI_HCI_COMMAND, data, length);
}

int t5ai_hci_ipc_send_acl(const uint8_t *data, size_t length) {
  return !prv_started() || prv_faulted() || data == NULL ? E_INVALID_OPERATION
                                    : prv_locked_exchange(T5AI_HCI_ACL, data, length);
}

int t5ai_hci_ipc_request_system_reboot(void) {
  t5ai_reboot_trace_event(T5RebootTrace_CPResetRequest, 0u);
  /* CPU1's local NVIC reset leaves CPU0 and the Bluetooth radio alive. Match
   * Tuya's AP reset path by asking CPU0 to perform the whole-chip reboot. The
   * request is intentionally asynchronous because a successful CPU0 handler
   * reboots before it can return a logical response. */
  s_system_reboot_request.message = (T5AIHCIMessage){
      .type = T5AI_SYSTEM_TYPE,
      .subtype = T5AI_SYSTEM_REBOOT,
  };
  return prv_prepare_outbound(&s_system_reboot_request,
                              &s_system_reboot_request.message,
                              sizeof(s_system_reboot_request.message), false);
}

bool t5ai_hci_ipc_faulted(void) {
  return prv_faulted();
}

void t5ai_hci_ipc_poll(void) {
  if (prv_started()) {
    (void)pbl_sem_give(&s_worker_wake);
  }
}

#ifdef CONFIG_T5AI_HCI_PROFILE
void t5ai_hci_ipc_get_diagnostics(T5AIHCIIpcDiagnostics *diagnostics) {
  if (diagnostics == NULL) {
    return;
  }
  *diagnostics = s_diagnostics;
  diagnostics->started = prv_started();
  diagnostics->faulted = prv_faulted();
  diagnostics->request_slots_in_use = 0u;
  diagnostics->response_slots_in_use = 0u;
  pbl_irq_lock();
  for (size_t i = 0; i < T5AI_HCI_REQUEST_CAPACITY; ++i) {
    diagnostics->request_slots_in_use += s_request_in_use[i] ? 1u : 0u;
  }
  for (size_t i = 0; i < T5AI_HCI_EVENT_RESPONSE_CAPACITY; ++i) {
    diagnostics->response_slots_in_use += s_event_response_in_use[i] ? 1u : 0u;
  }
  pbl_irq_unlock();
}
#endif
