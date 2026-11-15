/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/logging/logging.h>
PBL_LOG_MODULE_DEFINE(hci_t5ai, LOG_LEVEL_DEBUG);
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/kernel.h>
#include <pbl/soc/t5ai/flash_ipc.h>
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/power.h>

#include <kernel/pebble_tasks.h>

/* transport.h requires the mbuf definition first. */
#include <host/ble_hs.h>
#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <os/os_mbuf.h>

#define T5AI_HCI_TASK_STACK_BYTES            1024u
#define T5AI_HCI_TASK_PRIORITY               (PBL_PRIO_IDLE + 3u)
#define T5AI_HCI_ACL_RETRY_DELAY_MS          5u
#define T5AI_HCI_POLL_INTERVAL_MS            5u
#define T5AI_HCI_LE_CONNECTION_PREFIX_SIZE   6u
#define T5AI_HCI_LE_CONNECTION_COMPLETE_SIZE 21u
#define T5AI_HCI_COMMAND_RESULT_SIZE         6u
#define T5AI_HCI_READ_ADDRESS_COMPLETE_SIZE  12u
#define T5AI_HCI_ADV_PARAMS_SIZE             15u

// HCI packets omit the UART H4 type byte. Event offsets include the event header;
// advertising offsets address command parameters, excluding the command header.
enum {
  T5AI_HCI_EVENT_CODE_OFFSET = 0,
  T5AI_HCI_EVENT_LENGTH_OFFSET = 1,
  T5AI_HCI_LE_SUBEVENT_OFFSET = 2,
  T5AI_HCI_LE_STATUS_OFFSET = 3,
  T5AI_HCI_LE_HANDLE_OFFSET = 4,
  T5AI_HCI_LE_INTERVAL_OFFSET = 14,
  T5AI_HCI_LE_LATENCY_OFFSET = 16,
  T5AI_HCI_LE_TIMEOUT_OFFSET = 18,
  T5AI_HCI_COMPLETE_OPCODE_OFFSET = 3,
  T5AI_HCI_COMPLETE_STATUS_OFFSET = 5,
  T5AI_HCI_COMPLETE_ADDRESS_OFFSET = 6,
  T5AI_HCI_STATUS_STATUS_OFFSET = 2,
  T5AI_HCI_STATUS_OPCODE_OFFSET = 4,
  T5AI_HCI_ACL_LENGTH_OFFSET = 2,
  T5AI_HCI_ADV_ENABLE_OFFSET = 0,
  T5AI_HCI_ADV_MIN_INTERVAL_OFFSET = 0,
  T5AI_HCI_ADV_MAX_INTERVAL_OFFSET = 2,
  T5AI_HCI_ADV_TYPE_OFFSET = 4,
  T5AI_HCI_ADV_ADDRESS_TYPE_OFFSET = 5,
  T5AI_HCI_ADV_CHANNEL_MAP_OFFSET = 13,
  T5AI_HCI_ADV_FILTER_POLICY_OFFSET = 14,
};

#define T5AI_HCI_COMMAND_HEADER_SIZE 3u
#define T5AI_HCI_EVENT_HEADER_SIZE   2u
#define T5AI_HCI_ACL_HEADER_SIZE     4u
#define T5AI_HCI_MAX_COMMAND_SIZE    (T5AI_HCI_COMMAND_HEADER_SIZE + UINT8_MAX)
#define T5AI_HCI_MAX_EVENT_SIZE      (T5AI_HCI_EVENT_HEADER_SIZE + UINT8_MAX)
// The CP transport admits a 1024-byte ACL payload plus its four-byte HCI header.
#define T5AI_HCI_MAX_ACL_SIZE             (T5AI_HCI_ACL_HEADER_SIZE + 1024u)
#define T5AI_HCI_ACL_TX_QUEUE_CAPACITY    16u
#define T5AI_HCI_TRACE_INTERVAL_MS        30000u
#define T5AI_HCI_TX_DRAIN_TIMEOUT_MS      2500u
#define T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_ENABLE)

// Decode little-endian HCI fields without assuming alignment of the CP buffer.
static uint16_t prv_read_le16(const uint8_t *data, size_t offset) {
  return (uint16_t)data[offset] | ((uint16_t)data[offset + 1u] << 8);
}

#ifdef CONFIG_T5AI_FLASH_PROFILE
static void prv_log_flash_operation(const char *name, const T5AIFlashOperationDiagnostics *sample) {
  PBL_LOG_DBG(
      "T5AI flash timing: op=%s count=%lu max_ms=%lu last_ms=%lu tick=%lu offset=%lx len=%lu", name,
      (unsigned long)sample->count, (unsigned long)sample->max_ms, (unsigned long)sample->last_ms,
      (unsigned long)sample->last_tick, (unsigned long)sample->last_offset,
      (unsigned long)sample->last_length);
}

static void prv_log_flash_diagnostics(void) {
  T5AIFlashDiagnostics diagnostics;
  t5ai_flash_ipc_get_diagnostics(&diagnostics);
  prv_log_flash_operation("read", &diagnostics.read);
  prv_log_flash_operation("write", &diagnostics.write);
  prv_log_flash_operation("erase", &diagnostics.erase);
  PBL_LOG_DBG("T5AI flash active: command=%lu tick=%lu now=%lu",
              (unsigned long)diagnostics.active_command, (unsigned long)diagnostics.active_tick,
              (unsigned long)pbl_uptime_ticks());
}

#endif

static PBL_MUTEX_DEFINE(s_tx_mutex);
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
#define T5AI_HCI_QUIET_POLL_INTERVAL_MS 100u
static PBL_SEM_DEFINE(s_hci_work, 0, 1);

void t5ai_hci_notify_work(void) {
  (void)pbl_sem_give(&s_hci_work);
}
#endif

static bool s_started;
static bool s_reset_scheduled;
static bool s_acl_tx_active;
static uint8_t s_active_connections;
static uint8_t s_advertising_requested;
static uint8_t s_acl_tx[T5AI_HCI_MAX_ACL_SIZE];
PBL_THREAD_STACK_DEFINE(s_hci_task_stack, T5AI_HCI_TASK_STACK_BYTES);
static struct pbl_thread *s_hci_task;
static struct pbl_thread s_hci_thread;
static struct pbl_msgq *s_acl_tx_queue;
static PBL_MSGQ_DEFINE(s_acl_tx_messages, sizeof(struct os_mbuf *), T5AI_HCI_ACL_TX_QUEUE_CAPACITY);

#ifdef CONFIG_T5AI_HCI_PROFILE
static void prv_log_task_state(PebbleTask task) {
  struct pbl_thread *handle = pebble_task_get_thread(task);
  if (handle == NULL) {
    return;
  }
  struct pbl_thread_stack_info stack;
  pbl_thread_stack_info(handle, &stack);
  PBL_LOG_DBG("T5AI task snapshot: task=%c state=%u prio=%u stack=%u", pebble_task_get_char(task),
              (unsigned)pbl_thread_state(handle), (unsigned)pbl_thread_prio_get(handle),
              (unsigned)stack.high_water / sizeof(uint32_t));
}

#endif

static bool prv_started(void) {
  return __atomic_load_n(&s_started, __ATOMIC_ACQUIRE);
}

static void prv_set_started(bool started) {
  __atomic_store_n(&s_started, started, __ATOMIC_RELEASE);
}

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
bool t5ai_hci_sleep_ready(void) {
  return prv_started() && !__atomic_load_n(&s_reset_scheduled, __ATOMIC_ACQUIRE) &&
         !__atomic_load_n(&s_acl_tx_active, __ATOMIC_ACQUIRE) && s_acl_tx_queue != NULL &&
         pbl_msgq_num_used(s_acl_tx_queue) == 0u && t5ai_hci_ipc_sleep_ready();
}
#endif

static void prv_wait_for_work(void) {
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  const unsigned wait_ms =
      t5ai_hci_ipc_sleep_ready() ? T5AI_HCI_QUIET_POLL_INTERVAL_MS : T5AI_HCI_POLL_INTERVAL_MS;
  (void)pbl_sem_take(&s_hci_work, PBL_MSEC(wait_ms));
#else
  pbl_thread_sleep(PBL_MSEC(T5AI_HCI_POLL_INTERVAL_MS));
#endif
}

static void prv_schedule_reset(void) {
  bool expected = false;
  prv_set_started(false);
  if (__atomic_compare_exchange_n(&s_reset_scheduled, &expected, true, false, __ATOMIC_ACQ_REL,
                                  __ATOMIC_ACQUIRE)) {
    ble_hs_sched_reset(BLE_HS_ECONTROLLER);
  }
}

static bool prv_event_valid(const uint8_t *data, size_t length) {
  return data != NULL && length >= T5AI_HCI_EVENT_HEADER_SIZE &&
         length <= T5AI_HCI_MAX_EVENT_SIZE &&
         length == (size_t)data[T5AI_HCI_EVENT_LENGTH_OFFSET] + T5AI_HCI_EVENT_HEADER_SIZE;
}

static bool prv_acl_valid(const uint8_t *data, size_t length) {
  return data != NULL && length >= T5AI_HCI_ACL_HEADER_SIZE && length <= T5AI_HCI_MAX_ACL_SIZE &&
         length == T5AI_HCI_ACL_HEADER_SIZE + prv_read_le16(data, T5AI_HCI_ACL_LENGTH_OFFSET);
}

static void prv_send_queued_acl(struct os_mbuf *acl) {
  const size_t length = OS_MBUF_PKTLEN(acl);
  if (length > sizeof(s_acl_tx) || os_mbuf_copydata(acl, 0, length, s_acl_tx) != 0 ||
      !prv_acl_valid(s_acl_tx, length)) {
    PBL_LOG_ERR("Dropping invalid queued T5AI HCI ACL packet (%u bytes)", (unsigned)length);
    os_mbuf_free_chain(acl);
    return;
  }

  unsigned failures = 0;
  while (prv_started()) {
    const int status = t5ai_hci_ipc_send_acl(s_acl_tx, length);
    if (status == 0) {
      if (failures != 0u) {
        PBL_LOG_DBG("T5AI HCI ACL transmit recovered after %u retries", failures);
      }
      os_mbuf_free_chain(acl);
      return;
    }
    if (status == E_INVALID_OPERATION) {
      PBL_LOG_ERR("T5AI HCI transport fault; scheduling host reset");
      os_mbuf_free_chain(acl);
      prv_schedule_reset();
      return;
    }
    ++failures;
    PBL_LOG_WRN("T5AI HCI ACL transmit deferred: status=%d len=%u retry=%u", status,
                (unsigned)length, failures);
    t5ai_hci_ipc_poll();
    pbl_thread_sleep(PBL_MSEC(T5AI_HCI_ACL_RETRY_DELAY_MS));
  }
  os_mbuf_free_chain(acl);
}

static void prv_hci_task(void *unused) {
  (void)unused;
#if defined(CONFIG_T5AI_HCI_PROFILE) || defined(CONFIG_T5AI_FLASH_PROFILE)
  pbl_tick_t last_trace_tick = pbl_uptime_ticks();
#endif
  for (;;) {
    if (prv_started() && t5ai_hci_ipc_faulted()) {
      PBL_LOG_ERR("T5AI HCI worker detected transport fault");
      prv_schedule_reset();
    }
    struct os_mbuf *acl;
    pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
    const bool have_acl = prv_started() && s_acl_tx_queue != NULL &&
                          pbl_msgq_get(s_acl_tx_queue, &acl, PBL_NO_WAIT) == 0;
    __atomic_store_n(&s_acl_tx_active, have_acl, __ATOMIC_RELEASE);
    pbl_mutex_unlock(&s_tx_mutex);
    if (have_acl) {
      prv_send_queued_acl(acl);
      __atomic_store_n(&s_acl_tx_active, false, __ATOMIC_RELEASE);
      continue;
    }
    t5ai_hci_ipc_poll();

#if defined(CONFIG_T5AI_HCI_PROFILE) || defined(CONFIG_T5AI_FLASH_PROFILE)
    const pbl_tick_t now = pbl_uptime_ticks();
    if ((now - last_trace_tick) >= pbl_ms_to_ticks(T5AI_HCI_TRACE_INTERVAL_MS)) {
#ifdef CONFIG_T5AI_HCI_PROFILE
      T5AIHCIIpcDiagnostics diagnostics;
      t5ai_hci_ipc_get_diagnostics(&diagnostics);
      const unsigned queued =
          s_acl_tx_queue == NULL ? 0u : (unsigned)pbl_msgq_num_used(s_acl_tx_queue);
      PBL_LOG_DBG("T5AI HCI heartbeat: active=%u adv=%u queue=%u started=%u fault=%u",
                  s_active_connections, s_advertising_requested, queued, diagnostics.started,
                  diagnostics.faulted);
      PBL_LOG_DBG(
          "T5AI HCI heartbeat counters: rx=%lu evt=%lu acl=%lu cmd=%lu busy=%lu",
          (unsigned long)diagnostics.frames_received, (unsigned long)diagnostics.events_received,
          (unsigned long)diagnostics.acl_received, (unsigned long)diagnostics.commands_received,
          (unsigned long)diagnostics.send_busy_retries);
#endif
#ifdef CONFIG_T5AI_FLASH_PROFILE
      prv_log_flash_diagnostics();
#endif
      last_trace_tick = now;
    }
#endif
    prv_wait_for_work();
  }
}

static bool prv_discardable_event(const uint8_t *data, size_t length) {
  return prv_event_valid(data, length) && length >= 3u && data[0] == BLE_HCI_EVCODE_LE_META &&
         (data[2] == BLE_HCI_LE_SUBEV_ADV_RPT || data[2] == BLE_HCI_LE_SUBEV_EXT_ADV_RPT);
}

static bool prv_deliver_event(const uint8_t *data, size_t length) {
  if (!prv_started() || !prv_event_valid(data, length)) {
    PBL_LOG_ERR("Dropping invalid T5AI HCI event (%u bytes)", (unsigned)length);
    return true;
  }
  void *event = ble_transport_alloc_evt(prv_discardable_event(data, length));
  if (event == NULL) {
    return false;
  }
  // The CP survives an AP-only reset. Its Bluetooth reset can therefore emit
  // a Disconnection Complete for a link owned by the previous AP generation.
  // A fresh NimBLE host has no matching connection and treats that stale event
  // as a controller fault. Connection Complete always precedes a legitimate
  // disconnect in the current generation, so filter only that stale case.
  if (data[T5AI_HCI_EVENT_CODE_OFFSET] == BLE_HCI_EVCODE_LE_META &&
      length >= T5AI_HCI_LE_CONNECTION_PREFIX_SIZE &&
      (data[T5AI_HCI_LE_SUBEVENT_OFFSET] == BLE_HCI_LE_SUBEV_CONN_COMPLETE ||
       data[T5AI_HCI_LE_SUBEVENT_OFFSET] == BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE) &&
      data[T5AI_HCI_LE_STATUS_OFFSET] == 0u) {
    if (s_active_connections != UINT8_MAX) {
      ++s_active_connections;
    }
  }

  if (data[T5AI_HCI_EVENT_CODE_OFFSET] == BLE_HCI_EVCODE_LE_META &&
      length >= T5AI_HCI_LE_CONNECTION_COMPLETE_SIZE &&
      data[T5AI_HCI_LE_SUBEVENT_OFFSET] == BLE_HCI_LE_SUBEV_CONN_COMPLETE) {
    PBL_LOG_DBG("T5AI link result: status=%u handle=%u interval=%u latency=%u timeout=%u",
                data[T5AI_HCI_LE_STATUS_OFFSET], prv_read_le16(data, T5AI_HCI_LE_HANDLE_OFFSET),
                prv_read_le16(data, T5AI_HCI_LE_INTERVAL_OFFSET),
                prv_read_le16(data, T5AI_HCI_LE_LATENCY_OFFSET),
                prv_read_le16(data, T5AI_HCI_LE_TIMEOUT_OFFSET));
  }

  if (data[T5AI_HCI_EVENT_CODE_OFFSET] == BLE_HCI_EVCODE_DISCONN_CMP) {
#ifdef CONFIG_T5AI_FLASH_PROFILE
    prv_log_flash_diagnostics();
#endif
#ifdef CONFIG_T5AI_HCI_PROFILE
    T5AIHCIIpcDiagnostics diagnostics;
    t5ai_hci_ipc_get_diagnostics(&diagnostics);
    PBL_LOG_DBG(
        "T5AI HCI disconnect snapshot: rx=%lu ack=%lu cmd=%lu evt=%lu acl=%lu rsp=%lu mack=%lu",
        (unsigned long)diagnostics.frames_received,
        (unsigned long)diagnostics.transport_acks_received,
        (unsigned long)diagnostics.commands_received, (unsigned long)diagnostics.events_received,
        (unsigned long)diagnostics.acl_received, (unsigned long)diagnostics.responses_sent,
        (unsigned long)diagnostics.mipc_acks_sent);
    PBL_LOG_DBG("T5AI HCI pressure: busy=%lu sendto=%lu exchto=%lu req=%u/%lu rsp=%u/%lu",
                (unsigned long)diagnostics.send_busy_retries,
                (unsigned long)diagnostics.send_timeouts,
                (unsigned long)diagnostics.exchange_timeouts, diagnostics.request_slots_in_use,
                (unsigned long)diagnostics.request_exhaustions, diagnostics.response_slots_in_use,
                (unsigned long)diagnostics.response_exhaustions);
    PBL_LOG_DBG("T5AI HCI queue: pend=%u/%lu started=%u fault=%u",
                diagnostics.pending_queue_high_water,
                (unsigned long)diagnostics.pending_queue_exhaustions, diagnostics.started,
                diagnostics.faulted);
    prv_log_task_state(PebbleTask_KernelMain);
    prv_log_task_state(PebbleTask_KernelBackground);
    prv_log_task_state(PebbleTask_App);
    prv_log_task_state(PebbleTask_BTHost);
    prv_log_task_state(PebbleTask_BTHCI);

#endif

    if (s_active_connections == 0u) {
      ble_transport_free(event);
      PBL_LOG_WRN("Dropping stale T5AI HCI disconnect from prior AP generation");
      return true;
    }
    --s_active_connections;
  }

  if (data[T5AI_HCI_EVENT_CODE_OFFSET] == BLE_HCI_EVCODE_COMMAND_COMPLETE &&
      length >= T5AI_HCI_COMMAND_RESULT_SIZE) {
    const uint16_t opcode = prv_read_le16(data, T5AI_HCI_COMPLETE_OPCODE_OFFSET);
    if (data[T5AI_HCI_COMPLETE_STATUS_OFFSET] != 0u) {
      PBL_LOG_WRN("T5AI HCI command rejected: opcode=%04x status=%u", opcode,
                  data[T5AI_HCI_COMPLETE_STATUS_OFFSET]);
    }
    if (opcode == BLE_HCI_OP(BLE_HCI_OGF_INFO_PARAMS, BLE_HCI_OCF_IP_RD_BD_ADDR) &&
        length >= T5AI_HCI_READ_ADDRESS_COMPLETE_SIZE &&
        data[T5AI_HCI_COMPLETE_STATUS_OFFSET] == 0u) {
      PBL_LOG_DBG(
          "T5AI controller address: %02x:%02x:%02x:%02x:%02x:%02x",
          data[T5AI_HCI_COMPLETE_ADDRESS_OFFSET + 5u], data[T5AI_HCI_COMPLETE_ADDRESS_OFFSET + 4u],
          data[T5AI_HCI_COMPLETE_ADDRESS_OFFSET + 3u], data[T5AI_HCI_COMPLETE_ADDRESS_OFFSET + 2u],
          data[T5AI_HCI_COMPLETE_ADDRESS_OFFSET + 1u], data[T5AI_HCI_COMPLETE_ADDRESS_OFFSET]);
    }
    if (opcode == T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE) {
      PBL_LOG_DBG("T5AI HCI advertising command complete: status=%u",
                  data[T5AI_HCI_COMPLETE_STATUS_OFFSET]);
    }
  } else if (data[T5AI_HCI_EVENT_CODE_OFFSET] == BLE_HCI_EVCODE_COMMAND_STATUS &&
             length >= T5AI_HCI_COMMAND_RESULT_SIZE) {
    const uint16_t opcode = prv_read_le16(data, T5AI_HCI_STATUS_OPCODE_OFFSET);
    if (opcode == T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE) {
      PBL_LOG_DBG("T5AI HCI advertising command status: status=%u",
                  data[T5AI_HCI_STATUS_STATUS_OFFSET]);
    }
  }

  memcpy(event, data, length);
  const int result = ble_transport_to_hs_evt(event);
  if (result != 0) {
    // NimBLE consumes the event on both success and failure.
    PBL_LOG_ERR("T5AI HCI event delivery failed: %d", result);
    prv_schedule_reset();
  }
  return true;
}

static bool prv_deliver_acl(const uint8_t *data, size_t length) {
  if (!prv_started() || !prv_acl_valid(data, length)) {
    PBL_LOG_ERR("Dropping invalid T5AI HCI ACL packet (%u bytes)", (unsigned)length);
    return true;
  }

  struct os_mbuf *acl = ble_transport_alloc_acl_from_ll();
  if (acl == NULL || os_mbuf_append(acl, data, length) != 0) {
    if (acl != NULL) {
      os_mbuf_free_chain(acl);
    }
    return false;
  }
  const int result = ble_transport_to_hs_acl(acl);
  if (result != 0) {
    /* ble_transport_to_hs_acl() consumes acl regardless of outcome. */
    PBL_LOG_ERR("T5AI HCI ACL delivery failed: %d", result);
    prv_schedule_reset();
  }
  return true;
}

static bool prv_receive_event(const uint8_t *data, size_t length) {
  if (prv_started() && !prv_event_valid(data, length)) {
    prv_schedule_reset();
    return true;
  }
  return prv_deliver_event(data, length) || prv_discardable_event(data, length);
}

static bool prv_receive_acl(const uint8_t *data, size_t length) {
  if (prv_started() && !prv_acl_valid(data, length)) {
    prv_schedule_reset();
    return true;
  }
  return prv_deliver_acl(data, length);
}

void ble_transport_ll_reinit(void) {
  const T5AIHCICallbacks callbacks = {
    .event = prv_receive_event,
    .acl = prv_receive_acl,
    .discardable_event = prv_discardable_event,
  };

  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
  if (__atomic_load_n(&s_acl_tx_active, __ATOMIC_ACQUIRE)) {
    pbl_mutex_unlock(&s_tx_mutex);
    return;
  }
  __atomic_store_n(&s_reset_scheduled, false, __ATOMIC_RELEASE);
  s_active_connections = 0u;
  s_advertising_requested = 0u;
  prv_set_started(true);
  pbl_mutex_unlock(&s_tx_mutex);
  if (t5ai_hci_ipc_start(&callbacks) != 0) {
    prv_set_started(false);
    PBL_LOG_ERR("T5AI controller start failed");
  }
}

void ble_transport_ll_init(void) {
  t5ai_power_init();
  // CP shell readiness precedes flash/HCI readiness. Preserve the qualified delay.
  pbl_thread_sleep(PBL_MSEC(2000));

  if (s_acl_tx_queue == NULL) {
    s_acl_tx_queue = &s_acl_tx_messages;
  }
  ble_transport_ll_reinit();
  if (s_hci_task == NULL) {
    struct pbl_thread_attr parameters = {
      .entry = prv_hci_task,
      .name = "T5AIHCI",
      .stack = s_hci_task_stack,
      .stack_size = sizeof(s_hci_task_stack),
      .prio = T5AI_HCI_TASK_PRIORITY,
      .privileged = true,
    };
    if (pbl_thread_create(&s_hci_thread, &parameters) == 0) {
      s_hci_task = &s_hci_thread;
      pebble_task_register(PebbleTask_BTHCI, s_hci_task);
    }
    if (s_hci_task == NULL) {
      prv_set_started(false);
      PBL_LOG_ERR("T5AI HCI polling task creation failed");
    }
  }
}

void ble_transport_ll_deinit(void) {
  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
  prv_set_started(false);
  pbl_mutex_unlock(&s_tx_mutex);
  if (s_acl_tx_queue != NULL) {
    struct os_mbuf *acl;
    while (pbl_msgq_get(s_acl_tx_queue, &acl, PBL_NO_WAIT) == 0) {
      os_mbuf_free_chain(acl);
    }
  }
  // An old TX worker may still own a dequeued fragment. Let its bounded IPC
  // exchange finish before resetting the controller or admitting a new session.
  const pbl_tick_t start = pbl_uptime_ticks();
  while (__atomic_load_n(&s_acl_tx_active, __ATOMIC_ACQUIRE) &&
         pbl_uptime_ticks() - start < pbl_ms_to_ticks(T5AI_HCI_TX_DRAIN_TIMEOUT_MS)) {
    pbl_thread_sleep(PBL_MSEC(T5AI_HCI_POLL_INTERVAL_MS));
  }
  if (__atomic_load_n(&s_acl_tx_active, __ATOMIC_ACQUIRE)) {
    PBL_LOG_ERR("T5AI HCI old TX did not retire; restart blocked");
    return;
  }
  if (t5ai_hci_ipc_stop() != 0) {
    PBL_LOG_ERR("T5AI controller stop failed");
  }
}

int ble_transport_to_ll_cmd_impl(void *buf) {
  const struct ble_hci_cmd *command = buf;
  const size_t length = sizeof(*command) + command->length;
  int result = BLE_ERR_MEM_CAPACITY;

  if (command->opcode == BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_PARAMS) &&
      command->length == T5AI_HCI_ADV_PARAMS_SIZE) {
    const uint8_t *p = command->data;
    PBL_LOG_DBG("T5AI advertising parameters: min=%u max=%u type=%u addr=%u channels=%u filter=%u",
                prv_read_le16(p, T5AI_HCI_ADV_MIN_INTERVAL_OFFSET),
                prv_read_le16(p, T5AI_HCI_ADV_MAX_INTERVAL_OFFSET), p[T5AI_HCI_ADV_TYPE_OFFSET],
                p[T5AI_HCI_ADV_ADDRESS_TYPE_OFFSET], p[T5AI_HCI_ADV_CHANNEL_MAP_OFFSET],
                p[T5AI_HCI_ADV_FILTER_POLICY_OFFSET]);
  }

  const bool is_advert_enable =
      command->opcode == T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE && command->length >= 1u;
  if (is_advert_enable) {
    PBL_LOG_DBG("T5AI HCI advertising request: enable=%u",
                command->data[T5AI_HCI_ADV_ENABLE_OFFSET]);
  }

  // NimBLE clears old connections/credits, then starts resync with HCI Reset.
  // Recover here on the host task, never on the IPC worker that services start.
  if (!prv_started() && __atomic_load_n(&s_reset_scheduled, __ATOMIC_ACQUIRE) &&
      command->opcode == BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_RESET)) {
    ble_transport_ll_deinit();
    ble_transport_ll_reinit();
  }
  const int status = prv_started() && length <= T5AI_HCI_MAX_COMMAND_SIZE
                         ? t5ai_hci_ipc_send_command(buf, length)
                         : E_INVALID_OPERATION;
  if (status == 0) {
    result = 0;
    if (is_advert_enable) {
      s_advertising_requested = command->data[T5AI_HCI_ADV_ENABLE_OFFSET];
    }
  } else {
    PBL_LOG_ERR("T5AI HCI command transmit failed (%u bytes)", (unsigned)length);
    if (t5ai_hci_ipc_faulted()) {
      prv_schedule_reset();
    }
  }
  if (is_advert_enable && status != 0) {
    PBL_LOG_ERR("T5AI HCI advertising request transport failed: status=%d", status);
  }
  ble_transport_free(buf);
  return result;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *acl) {
  const size_t length = OS_MBUF_PKTLEN(acl);
  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
  if (!prv_started() && __atomic_load_n(&s_reset_scheduled, __ATOMIC_ACQUIRE)) {
    // The scheduled reset retires this connection and its credits. Consume old
    // host TX without returning an error into an in-progress ATT response.
    pbl_mutex_unlock(&s_tx_mutex);
    os_mbuf_free_chain(acl);
    return 0;
  }
  const bool queued = prv_started() && s_acl_tx_queue != NULL && length <= T5AI_HCI_MAX_ACL_SIZE &&
                      pbl_msgq_put(s_acl_tx_queue, &acl, PBL_NO_WAIT) == 0;
  pbl_mutex_unlock(&s_tx_mutex);
  if (queued) {
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
    t5ai_hci_notify_work();
#endif
    return 0;
  }
  PBL_LOG_ERR("T5AI HCI ACL transmit queue full (%u bytes)", (unsigned)length);
  os_mbuf_free_chain(acl);
  return BLE_ERR_MEM_CAPACITY;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *iso) {
  os_mbuf_free_chain(iso);
  return BLE_ERR_UNSUPPORTED;
}
