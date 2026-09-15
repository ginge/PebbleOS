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

#define T5AI_HCI_COMMAND_HEADER_SIZE 3u
#define T5AI_HCI_EVENT_HEADER_SIZE 2u
#define T5AI_HCI_ACL_HEADER_SIZE 4u
#define T5AI_HCI_MAX_COMMAND_SIZE 258u
#define T5AI_HCI_MAX_EVENT_SIZE 257u
#define T5AI_HCI_MAX_ACL_SIZE 1028u
#define T5AI_HCI_ACL_TX_QUEUE_CAPACITY 16u
#define T5AI_L2CAP_CID_ATT 0x0004u
#define T5AI_ATT_OP_HANDLE_VALUE_NOTIFICATION 0x1bu
#define T5AI_ATT_OP_WRITE_COMMAND 0x52u
#define T5AI_PPOG_PACKET_TYPE_MASK 0x07u
#define T5AI_PPOG_PACKET_TYPE_DATA 0u
#define T5AI_PPOG_PACKET_TYPE_ACK 1u
#define T5AI_PPOG_SEQUENCE_SHIFT 3u

static bool s_started;
static uint8_t s_acl_tx[T5AI_HCI_MAX_ACL_SIZE];
static TaskHandle_t s_hci_task;
static QueueHandle_t s_acl_tx_queue;
static uint32_t s_pp_rx_remaining;
static uint32_t s_pp_rx_packets;

static uint16_t prv_read_le16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t prv_read_be16(const uint8_t *data) {
  return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

static void prv_trace_ppog_rx(const uint8_t *data, size_t length) {
  /* HCI ACL (4), L2CAP (4), ATT Write Command (3), then PPoG. */
  if (length < 12u || prv_read_le16(&data[6]) != T5AI_L2CAP_CID_ATT ||
      data[8] != T5AI_ATT_OP_WRITE_COMMAND) {
    return;
  }

  const uint8_t header = data[11];
  const unsigned type = header & T5AI_PPOG_PACKET_TYPE_MASK;
  const unsigned sequence = header >> T5AI_PPOG_SEQUENCE_SHIFT;
  const uint8_t *payload = &data[12];
  const size_t payload_length = length - 12u;

  if (type != T5AI_PPOG_PACKET_TYPE_DATA) {
    PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
              "T5AI PPoG RX control type=%u sn=%u len=%u", type, sequence,
              (unsigned)payload_length);
    if (type != T5AI_PPOG_PACKET_TYPE_ACK) {
      s_pp_rx_remaining = 0;
    }
    return;
  }

  ++s_pp_rx_packets;
  if (s_pp_rx_remaining == 0 && payload_length >= 4u) {
    const uint16_t pp_length = prv_read_be16(payload);
    const uint16_t endpoint = prv_read_be16(&payload[2]);
    s_pp_rx_remaining = (uint32_t)pp_length + 4u;
    PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
              "T5AI PP RX start ep=0x%04x len=%u sn=%u packet=%lu", endpoint,
              pp_length, sequence, (unsigned long)s_pp_rx_packets);
  } else if ((s_pp_rx_packets & 31u) == 0u) {
    PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
              "T5AI PPoG RX progress packet=%lu remaining=%lu sn=%u",
              (unsigned long)s_pp_rx_packets, (unsigned long)s_pp_rx_remaining, sequence);
  }

  if (payload_length >= s_pp_rx_remaining) {
    s_pp_rx_remaining = 0;
  } else {
    s_pp_rx_remaining -= payload_length;
  }
}

static void prv_trace_ppog_tx(const uint8_t *data, size_t length) {
  /* HCI ACL (4), L2CAP (4), ATT Handle Value Notification (3), then PPoG. */
  if (length < 12u || prv_read_le16(&data[6]) != T5AI_L2CAP_CID_ATT ||
      data[8] != T5AI_ATT_OP_HANDLE_VALUE_NOTIFICATION) {
    return;
  }

  const uint8_t header = data[11];
  const unsigned type = header & T5AI_PPOG_PACKET_TYPE_MASK;
  if (type != T5AI_PPOG_PACKET_TYPE_DATA) {
    PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
              "T5AI PPoG TX control type=%u sn=%u", type,
              header >> T5AI_PPOG_SEQUENCE_SHIFT);
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

  prv_trace_ppog_tx(s_acl_tx, length);
  unsigned failures = 0;
  while (s_started) {
    const int status = t5ai_hci_ipc_send_acl(s_acl_tx, length);
    if (status == 0) {
      if (failures != 0u) {
        PBL_LOG_D_INFO(LOG_DOMAIN_COMM,
                       "T5AI HCI ACL transmit recovered after %u retries", failures);
      }
      os_mbuf_free_chain(acl);
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
  for (;;) {
    struct os_mbuf *acl;
    if (s_acl_tx_queue != NULL && xQueueReceive(s_acl_tx_queue, &acl, 0) == pdTRUE) {
      prv_send_queued_acl(acl);
      continue;
    }
    t5ai_hci_ipc_poll();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void prv_receive_event(const uint8_t *data, size_t length) {
  if (!s_started || !prv_event_valid(data, length)) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "Dropping invalid T5AI HCI event (%u bytes)",
                  (unsigned)length);
    return;
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
  if (!s_started || !prv_acl_valid(data, length)) {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "Dropping invalid T5AI HCI ACL packet (%u bytes)",
                  (unsigned)length);
    return;
  }

  prv_trace_ppog_rx(data, length);

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

  s_started = true;
  if (t5ai_hci_ipc_start(&callbacks) != 0) {
    s_started = false;
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
      s_started = false;
      PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI polling task creation failed");
    }
  }
}

void ble_transport_ll_deinit(void) {
  s_started = false;
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

  if (s_started && length <= T5AI_HCI_MAX_COMMAND_SIZE &&
      t5ai_hci_ipc_send_command(buf, length) == 0) {
    result = 0;
  } else {
    PBL_LOG_D_ERR(LOG_DOMAIN_COMM, "T5AI HCI command transmit failed (%u bytes)",
                  (unsigned)length);
  }
  ble_transport_free(buf);
  return result;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *acl) {
  const size_t length = OS_MBUF_PKTLEN(acl);
  if (s_started && s_acl_tx_queue != NULL && length <= T5AI_HCI_MAX_ACL_SIZE &&
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
