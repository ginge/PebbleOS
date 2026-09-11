/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/hci_ipc.h>

#include <kernel/pebble_tasks.h>

#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* transport.h requires the mbuf definition first. */
#include <os/os_mbuf.h>
#include <nimble/hci_common.h>
#include <nimble/transport.h>

#define T5AI_HCI_COMMAND_HEADER_SIZE 3u
#define T5AI_HCI_EVENT_HEADER_SIZE 2u
#define T5AI_HCI_ACL_HEADER_SIZE 4u
#define T5AI_HCI_MAX_COMMAND_SIZE 258u
#define T5AI_HCI_MAX_EVENT_SIZE 257u
#define T5AI_HCI_MAX_ACL_SIZE 1028u

static bool s_started;
static bool s_failed;
static uint8_t s_acl_tx[T5AI_HCI_MAX_ACL_SIZE];
static TaskHandle_t s_hci_task;

static void prv_hci_task(void *unused) {
  (void)unused;
  for (;;) {
    t5ai_hci_ipc_poll();
    vTaskDelay(pdMS_TO_TICKS(5));
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

static void prv_receive_event(const uint8_t *data, size_t length) {
  if (!s_started || s_failed || !prv_event_valid(data, length)) {
    s_failed = true;
    return;
  }

  void *event = ble_transport_alloc_evt(0);
  if (event == NULL) {
    s_failed = true;
    PBL_LOG_D_ERR(LOG_DOMAIN_BT_STACK, "T5AI HCI event pool exhausted");
    return;
  }
  memcpy(event, data, length);
  if (ble_transport_to_hs_evt(event) != 0) {
    s_failed = true;
  }
}

static void prv_receive_acl(const uint8_t *data, size_t length) {
  if (!s_started || s_failed || !prv_acl_valid(data, length)) {
    s_failed = true;
    return;
  }

  struct os_mbuf *acl = ble_transport_alloc_acl_from_ll();
  if (acl == NULL || os_mbuf_append(acl, data, length) != 0) {
    if (acl != NULL) {
      os_mbuf_free_chain(acl);
    }
    s_failed = true;
    PBL_LOG_D_ERR(LOG_DOMAIN_BT_STACK, "T5AI HCI ACL pool exhausted");
    return;
  }
  if (ble_transport_to_hs_acl(acl) != 0) {
    s_failed = true;
  }
}

void ble_transport_ll_reinit(void) {
  const T5AIHCICallbacks callbacks = {
      .event = prv_receive_event,
      .acl = prv_receive_acl,
  };

  s_failed = false;
  s_started = true;
  if (t5ai_hci_ipc_start(&callbacks) != 0) {
    s_started = false;
    s_failed = true;
    PBL_LOG_D_ERR(LOG_DOMAIN_BT_STACK, "T5AI controller start failed");
  }
}

void ble_transport_ll_init(void) {
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
      s_failed = true;
    }
  }
}

void ble_transport_ll_deinit(void) {
  s_started = false;
  if (t5ai_hci_ipc_stop() != 0) {
    s_failed = true;
  }
}

int ble_transport_to_ll_cmd_impl(void *buf) {
  const struct ble_hci_cmd *command = buf;
  const size_t length = sizeof(*command) + command->length;
  int result = BLE_ERR_MEM_CAPACITY;

  if (s_started && !s_failed && length <= T5AI_HCI_MAX_COMMAND_SIZE &&
      t5ai_hci_ipc_send_command(buf, length) == 0) {
    result = 0;
  } else {
    s_failed = true;
  }
  ble_transport_free(buf);
  return result;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *acl) {
  const size_t length = OS_MBUF_PKTLEN(acl);
  int result = BLE_ERR_MEM_CAPACITY;

  if (s_started && !s_failed && length <= sizeof(s_acl_tx) &&
      os_mbuf_copydata(acl, 0, length, s_acl_tx) == 0 && prv_acl_valid(s_acl_tx, length) &&
      t5ai_hci_ipc_send_acl(s_acl_tx, length) == 0) {
    result = 0;
  } else {
    s_failed = true;
  }
  os_mbuf_free_chain(acl);
  return result;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *iso) {
  os_mbuf_free_chain(iso);
  return BLE_ERR_UNSUPPORTED;
}
