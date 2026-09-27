/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/hci_ipc.h>

#include <kernel/pebble_tasks.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* transport.h requires the mbuf definition first. */
#include <os/os_mbuf.h>
#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <host/ble_hs.h>

#define T5AI_HCI_COMMAND_HEADER_SIZE 3u
#define T5AI_HCI_EVENT_HEADER_SIZE 2u
#define T5AI_HCI_ACL_HEADER_SIZE 4u
#define T5AI_HCI_MAX_COMMAND_SIZE 258u
#define T5AI_HCI_MAX_EVENT_SIZE 257u
#define T5AI_HCI_MAX_ACL_SIZE 1028u
#define T5AI_HCI_ACL_TX_QUEUE_CAPACITY 16u
#define T5AI_HCI_TRACE_INTERVAL_MS 30000u
#define T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE \
  BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_ENABLE)

static bool s_started;
static bool s_reset_scheduled;
static uint8_t s_active_connections;
static uint8_t s_advertising_requested;
static uint8_t s_acl_tx[T5AI_HCI_MAX_ACL_SIZE];
static TaskHandle_t s_hci_task;
static QueueHandle_t s_acl_tx_queue;

static void prv_log_task_state(PebbleTask task) {
  TaskHandle_t handle = pebble_task_get_handle_for_task(task);
  if (handle == NULL) {
    return;
  }
  PBL_LOG_D_INFO(LOG_DOMAIN_COMM, "T5AI task snapshot: task=%c state=%u prio=%u stack=%u",
                 pebble_task_get_char(task), (unsigned)eTaskGetState(handle),
                 (unsigned)uxTaskPriorityGet(handle),
                 (unsigned)uxTaskGetStackHighWaterMark(handle));
}

static bool prv_started(void) {
  return __atomic_load_n(&s_started, __ATOMIC_ACQUIRE);
}

static void prv_set_started(bool started) {
  __atomic_store_n(&s_started, started, __ATOMIC_RELEASE);
}

static void prv_schedule_reset(void) {
  bool expected = false;
  prv_set_started(false);
  if (__atomic_compare_exchange_n(&s_reset_scheduled, &expected, true, false,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    ble_hs_sched_reset(BLE_HS_ECONTROLLER);
  }
}

static bool prv_event_valid(const uint8_t *data, size_t length) {
  return data != NULL && length >= T5AI_HCI_EVENT_HEADER_SIZE &&
         length <= T5AI_HCI_MAX_EVENT_SIZE &&
         length == (size_t)data[1] + T5AI_HCI_EVENT_HEADER_SIZE;
}

static bool prv_acl_valid(const uint8_t *data, size_t length) {
  return data != NULL && length >= T5AI_HCI_ACL_HEADER_SIZE && length <= T5AI_HCI_MAX_ACL_SIZE &&
         length == T5AI_HCI_ACL_HEADER_SIZE + data[2] + ((size_t)data[3] << 8);
}

static void prv_send_queued_acl(struct os_mbuf *acl) {
  const size_t length = OS_MBUF_PKTLEN(acl);
  if (length > sizeof(s_acl_tx) || os_mbuf_copydata(acl, 0, length, s_acl_tx) != 0 ||
      !prv_acl_valid(s_acl_tx, length)) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "Dropping invalid queued T5AI HCI ACL packet (%u bytes)",
                  (unsigned)length);
    os_mbuf_free_chain(acl);
    return;
  }

  unsigned failures = 0;
  while (prv_started()) {
    const int status = t5ai_hci_ipc_send_acl(s_acl_tx, length);
    if (status == 0) {
      if (failures != 0u) {
        PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                       "T5AI HCI ACL transmit recovered after %u retries", failures);
      }
      os_mbuf_free_chain(acl);
      return;
    }
    if (status == E_INVALID_OPERATION) {
      PBL_LOG_D_ERR(LOG_DOMAIN_COMM,
                    "T5AI HCI transport fault; scheduling host reset");
      os_mbuf_free_chain(acl);
      prv_schedule_reset();
      return;
    }
    ++failures;
    PBL_LOG_D_WRN(LOG_DOMAIN_COMM,
                  "T5AI HCI ACL transmit deferred: status=%d len=%u retry=%u", status,
                  (unsigned)length, failures);
    t5ai_hci_ipc_poll();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  os_mbuf_free_chain(acl);
}

static void prv_hci_task(void *unused) {
  (void)unused;
  TickType_t last_trace_tick = xTaskGetTickCount();
  for (;;) {
    if (prv_started() && t5ai_hci_ipc_faulted()) {
      PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI worker detected transport fault");
      prv_schedule_reset();
    }
    struct os_mbuf *acl;
    if (s_acl_tx_queue != NULL && xQueueReceive(s_acl_tx_queue, &acl, 0) == pdTRUE) {
      prv_send_queued_acl(acl);
      continue;
    }
    t5ai_hci_ipc_poll();

    const TickType_t now = xTaskGetTickCount();
    if ((now - last_trace_tick) >= pdMS_TO_TICKS(T5AI_HCI_TRACE_INTERVAL_MS)) {
      T5AIHCIIpcDiagnostics diagnostics;
      t5ai_hci_ipc_get_diagnostics(&diagnostics);
      const unsigned queued = s_acl_tx_queue == NULL ? 0u :
          (unsigned)uxQueueMessagesWaiting(s_acl_tx_queue);
      PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                     "T5AI HCI heartbeat: active=%u adv=%u queue=%u started=%u fault=%u",
                     s_active_connections, s_advertising_requested, queued,
                     diagnostics.started, diagnostics.faulted);
      PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                     "T5AI HCI heartbeat counters: rx=%lu evt=%lu acl=%lu cmd=%lu busy=%lu",
                     (unsigned long)diagnostics.frames_received,
                     (unsigned long)diagnostics.events_received,
                     (unsigned long)diagnostics.acl_received,
                     (unsigned long)diagnostics.commands_received,
                     (unsigned long)diagnostics.send_busy_retries);
      last_trace_tick = now;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void prv_receive_event(const uint8_t *data, size_t length) {
  if (!prv_started() || !prv_event_valid(data, length)) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "Dropping invalid T5AI HCI event (%u bytes)",
                  (unsigned)length);
    return;
  }
  // The CP survives an AP-only reset. Its Bluetooth reset can therefore emit
  // a Disconnection Complete for a link owned by the previous AP generation.
  // A fresh NimBLE host has no matching connection and treats that stale event
  // as a controller fault. Connection Complete always precedes a legitimate
  // disconnect in the current generation, so filter only that stale case.
  if (data[0] == 0x3eu && length >= 6u &&
      (data[2] == 0x01u || data[2] == 0x0au) && data[3] == 0u) {
    if (s_active_connections != UINT8_MAX) {
      ++s_active_connections;
    }
  }

  if (data[0] == 0x05u) {
    T5AIHCIIpcDiagnostics diagnostics;
    t5ai_hci_ipc_get_diagnostics(&diagnostics);
    PBL_LOG_D_INFO(
        LOG_DOMAIN_COMM,
        "T5AI HCI disconnect snapshot: rx=%lu ack=%lu cmd=%lu evt=%lu acl=%lu rsp=%lu mack=%lu",
        (unsigned long)diagnostics.frames_received,
        (unsigned long)diagnostics.transport_acks_received,
        (unsigned long)diagnostics.commands_received, (unsigned long)diagnostics.events_received,
        (unsigned long)diagnostics.acl_received, (unsigned long)diagnostics.responses_sent,
        (unsigned long)diagnostics.mipc_acks_sent);
    PBL_LOG_D_INFO(
        LOG_DOMAIN_COMM,
        "T5AI HCI pressure: busy=%lu sendto=%lu exchto=%lu req=%u/%lu rsp=%u/%lu",
        (unsigned long)diagnostics.send_busy_retries, (unsigned long)diagnostics.send_timeouts,
        (unsigned long)diagnostics.exchange_timeouts, diagnostics.request_slots_in_use,
        (unsigned long)diagnostics.request_exhaustions, diagnostics.response_slots_in_use,
        (unsigned long)diagnostics.response_exhaustions);
    PBL_LOG_D_INFO(
        LOG_DOMAIN_COMM,
        "T5AI HCI queue: pend=%u/%lu started=%u fault=%u",
        diagnostics.pending_queue_high_water,
        (unsigned long)diagnostics.pending_queue_exhaustions, diagnostics.started,
        diagnostics.faulted);
    prv_log_task_state(PebbleTask_KernelMain);
    prv_log_task_state(PebbleTask_KernelBackground);
    prv_log_task_state(PebbleTask_App);
    prv_log_task_state(PebbleTask_BTHost);
    prv_log_task_state(PebbleTask_BTHCI);

    if (s_active_connections == 0u) {
      PBL_LOG_D_WRN(LOG_DOMAIN_COMM,
                    "Dropping stale T5AI HCI disconnect from prior AP generation");
      return;
    }
    --s_active_connections;
  }

  if (data[0] == 0x0eu && length >= 6u) {
    const uint16_t opcode = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    if (opcode == T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE) {
      PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                     "T5AI HCI advertising command complete: status=%u", data[5]);
    }
  } else if (data[0] == 0x0fu && length >= 6u) {
    const uint16_t opcode = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
    if (opcode == T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE) {
      PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                     "T5AI HCI advertising command status: status=%u", data[2]);
    }
  }

  void *event = ble_transport_alloc_evt(0);
  if (event == NULL) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI event pool exhausted");
    return;
  }
  memcpy(event, data, length);
  const int result = ble_transport_to_hs_evt(event);
  if (result != 0) {
    /* NimBLE consumes the event on both success and failure. A malformed or
     * stale controller event must not permanently disable unrelated outbound
     * ACL traffic; doing so makes the next ATT response hit rc == 0 asserts. */
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI event delivery failed: %d", result);
  }
}

static void prv_receive_acl(const uint8_t *data, size_t length) {
  if (!prv_started() || !prv_acl_valid(data, length)) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "Dropping invalid T5AI HCI ACL packet (%u bytes)",
                  (unsigned)length);
    return;
  }

  struct os_mbuf *acl = ble_transport_alloc_acl_from_ll();
  if (acl == NULL || os_mbuf_append(acl, data, length) != 0) {
    if (acl != NULL) {
      os_mbuf_free_chain(acl);
    }
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI ACL pool exhausted");
    return;
  }
  const int result = ble_transport_to_hs_acl(acl);
  if (result != 0) {
    /* ble_transport_to_hs_acl() consumes acl regardless of outcome. */
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI ACL delivery failed: %d", result);
  }
}

void ble_transport_ll_reinit(void) {
  const T5AIHCICallbacks callbacks = {
      .event = prv_receive_event,
      .acl = prv_receive_acl,
  };

  __atomic_store_n(&s_reset_scheduled, false, __ATOMIC_RELEASE);
  s_active_connections = 0u;
  s_advertising_requested = 0u;
  prv_set_started(true);
  if (t5ai_hci_ipc_start(&callbacks) != 0) {
    prv_set_started(false);
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI controller start failed");
  }
}

void ble_transport_ll_init(void) {
  if (s_acl_tx_queue == NULL) {
    s_acl_tx_queue = xQueueCreate(T5AI_HCI_ACL_TX_QUEUE_CAPACITY, sizeof(struct os_mbuf *));
  }
  ble_transport_ll_reinit();
  if (s_hci_task == NULL) {
    TaskParameters_t parameters = {
        .pvTaskCode = prv_hci_task,
        .pcName = "T5AIHCI",
        .usStackDepth = 1024 / sizeof(StackType_t),
        .uxPriority = (tskIDLE_PRIORITY + 3) | portPRIVILEGE_BIT,
    };
    pebble_task_create(PebbleTask_BTHCI, &parameters, &s_hci_task);
    if (s_hci_task == NULL) {
      prv_set_started(false);
      PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI polling task creation failed");
    }
  }
}

void ble_transport_ll_deinit(void) {
  prv_set_started(false);
  if (t5ai_hci_ipc_stop() != 0) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI controller stop failed");
  }
  if (s_acl_tx_queue != NULL) {
    struct os_mbuf *acl;
    while (xQueueReceive(s_acl_tx_queue, &acl, 0) == pdTRUE) {
      os_mbuf_free_chain(acl);
    }
  }
}

int ble_transport_to_ll_cmd_impl(void *buf) {
  const struct ble_hci_cmd *command = buf;
  const size_t length = sizeof(*command) + command->length;
  int result = BLE_ERR_MEM_CAPACITY;

  const bool is_advert_enable =
      command->opcode == T5AI_HCI_LE_SET_ADV_ENABLE_OPCODE && command->length >= 1u;
  if (is_advert_enable) {
    PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                   "T5AI HCI advertising request: enable=%u", command->data[0]);
  }

  const int status = prv_started() && length <= T5AI_HCI_MAX_COMMAND_SIZE
                         ? t5ai_hci_ipc_send_command(buf, length)
                         : E_INVALID_OPERATION;
  if (status == 0) {
    result = 0;
    if (is_advert_enable) {
      s_advertising_requested = command->data[0];
    }
  } else {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI command transmit failed (%u bytes)",
                  (unsigned)length);
    if (t5ai_hci_ipc_faulted()) {
      prv_schedule_reset();
    }
  }
  if (is_advert_enable && status != 0) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM,
                  "T5AI HCI advertising request transport failed: status=%d", status);
  }
  ble_transport_free(buf);
  return result;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *acl) {
  const size_t length = OS_MBUF_PKTLEN(acl);
  if (prv_started() && s_acl_tx_queue != NULL && length <= T5AI_HCI_MAX_ACL_SIZE &&
      xQueueSend(s_acl_tx_queue, &acl, 0) == pdTRUE) {
    return 0;
  }
  PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI ACL transmit queue full (%u bytes)",
                (unsigned)length);
  os_mbuf_free_chain(acl);
  return BLE_ERR_MEM_CAPACITY;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *iso) {
  os_mbuf_free_chain(iso);
  return BLE_ERR_UNSUPPORTED;
}
