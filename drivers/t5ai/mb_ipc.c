/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/cache.h>
PBL_LOG_MODULE_DEFINE(t5ai_mb_ipc, LOG_LEVEL_DEBUG);
#include <pbl/kernel/kernel.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/rtc.h>
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
#include <pbl/kernel/sched.h>
#include <pbl/soc/t5ai/io_trace.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// MB-IPC parameter1: dst port/CPU, src port/CPU, tag, API status, route status.
#define MB_IPC_PORT_MASK 0x3fu
#define MB_IPC_CPU_MASK 3u
#define MB_IPC_DEST_CPU_SHIFT 6u
#define MB_IPC_SOURCE_PORT_SHIFT 8u
#define MB_IPC_SOURCE_CPU_SHIFT 14u
#define MB_IPC_TAG_SHIFT 16u
#define MB_IPC_API_STATUS_SHIFT 24u
#define MB_IPC_ROUTE_STATUS_SHIFT 28u
#define MB_IPC_STATUS_MASK 0x0fu
#define MB_IPC_ROUTE_STATUS_MASK (MB_IPC_STATUS_MASK << MB_IPC_ROUTE_STATUS_SHIFT)
// parameter2: 16-bit byte length, CRC-8, then the service command.
#define MB_IPC_CRC_SHIFT 16u
#define MB_IPC_USER_COMMAND_SHIFT 24u
#define MB_IPC_MAX_PAYLOAD_BYTES UINT16_MAX
#define MB_IPC_CLIENT_PORT_MIN 16u
#define MB_IPC_CLIENT_PORT_MAX 63u
#define MB_IPC_SERVER_PORT_MAX 15u
#define MB_IPC_CONNECT_TIMEOUT_MS 500u
// Vendor MSB-first CRC-8, seed zero, no final XOR.
#define MB_IPC_CRC_POLYNOMIAL 0x31u
#define MB_IPC_CRC_HIGH_BIT 0x80u
#define MB_IPC_CRC_BITS 8u

#define MB_IPC_TX_CHANNEL 0x11u
#define MB_IPC_RX_CHANNEL 0x41u
#define MB_IPC_CPU_AP 1u
#define MB_IPC_CPU_CP 0u
#define MB_IPC_COMMAND_CONNECT 0u
#define MB_IPC_COMMAND_SEND 2u
#define MB_IPC_RESPONSE_FLAG 0x80u
#define MB_IPC_ROUTE_OK 0u
#define MB_IPC_ROUTE_UNREACHABLE 2u
#define MB_IPC_ROUTE_UNSOLICITED 3u
#define MB_IPC_ROUTE_UNMATCHED 4u
#define MB_IPC_ROUTE_RX_BUSY 5u
#define MB_IPC_SOCKET_COUNT 2u
#define MB_IPC_PHYSICAL_TIMEOUT_MS 5000u

struct T5AIMbIpcSocket {
  uint8_t client_port;
  uint8_t server_port;
  uint8_t tx_tag;
  uint8_t tx_command;
  uint32_t generation;
  uint32_t rx_ack_aon;
  bool allocated;
  bool connected;
  bool poisoned;
  bool tx_in_process;
  bool rx_in_process;
  T5AIMailboxMessage tx_message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
  T5AIMailboxMessage rx_message;
  status_t tx_status;
  struct pbl_mutex mutex;
  struct pbl_sem tx_ready;
  struct pbl_sem rx_ready;
};

static T5AIMbIpcSocket s_sockets[MB_IPC_SOCKET_COUNT];
static bool s_initialized;
static bool s_sync_initialized;

static uint8_t prv_crc8(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint8_t crc = 0u;
  while (length-- != 0u) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < MB_IPC_CRC_BITS; ++bit) {
      crc = (crc & MB_IPC_CRC_HIGH_BIT) != 0u ? (uint8_t)((crc << 1u) ^ MB_IPC_CRC_POLYNOMIAL)
                                              : (uint8_t)(crc << 1u);
    }
  }
  return crc;
}

static uint32_t prv_address(uint8_t destination_cpu, uint8_t destination_port, uint8_t source_cpu,
                            uint8_t source_port, uint8_t tag) {
  return (destination_port & MB_IPC_PORT_MASK) |
         ((uint32_t)(destination_cpu & MB_IPC_CPU_MASK) << MB_IPC_DEST_CPU_SHIFT) |
         ((uint32_t)(source_port & MB_IPC_PORT_MASK) << MB_IPC_SOURCE_PORT_SHIFT) |
         ((uint32_t)(source_cpu & MB_IPC_CPU_MASK) << MB_IPC_SOURCE_CPU_SHIFT) |
         ((uint32_t)tag << MB_IPC_TAG_SHIFT);
}

static uint8_t prv_destination_cpu(const T5AIMailboxMessage *message) {
  return (uint8_t)(message->parameter1 >> MB_IPC_DEST_CPU_SHIFT) & MB_IPC_CPU_MASK;
}

static uint8_t prv_destination_port(const T5AIMailboxMessage *message) {
  return (uint8_t)message->parameter1 & MB_IPC_PORT_MASK;
}

static uint8_t prv_source_cpu(const T5AIMailboxMessage *message) {
  return (uint8_t)(message->parameter1 >> MB_IPC_SOURCE_CPU_SHIFT) & MB_IPC_CPU_MASK;
}

static uint8_t prv_source_port(const T5AIMailboxMessage *message) {
  return (uint8_t)(message->parameter1 >> MB_IPC_SOURCE_PORT_SHIFT) & MB_IPC_PORT_MASK;
}

static uint8_t prv_tag(const T5AIMailboxMessage *message) {
  return (uint8_t)(message->parameter1 >> MB_IPC_TAG_SHIFT);
}

static uint8_t prv_route_status(const T5AIMailboxMessage *message) {
  return (uint8_t)(message->parameter1 >> MB_IPC_ROUTE_STATUS_SHIFT);
}

static uint8_t prv_api_status(const T5AIMailboxMessage *message) {
  return (uint8_t)(message->parameter1 >> MB_IPC_API_STATUS_SHIFT) & MB_IPC_STATUS_MASK;
}

static void prv_set_route_status(T5AIMailboxMessage *message, uint8_t route_status) {
  message->parameter1 =
      (message->parameter1 & ~MB_IPC_ROUTE_STATUS_MASK) |
      ((uint32_t)(route_status & MB_IPC_STATUS_MASK) << MB_IPC_ROUTE_STATUS_SHIFT);
}

static T5AIMbIpcSocket *prv_find_receiver(const T5AIMailboxMessage *message) {
  for (size_t i = 0; i < MB_IPC_SOCKET_COUNT; ++i) {
    T5AIMbIpcSocket *socket = &s_sockets[i];
    if (socket->allocated && !socket->poisoned && prv_destination_cpu(message) == MB_IPC_CPU_AP &&
        prv_source_cpu(message) == MB_IPC_CPU_CP &&
        prv_destination_port(message) == socket->client_port &&
        prv_source_port(message) == socket->server_port) {
      return socket;
    }
  }
  return NULL;
}

static void prv_receive_callback(uint8_t channel, const T5AICPMailboxFrame *frame, void *context) {
  (void)channel;
  (void)context;
  if (frame->length != sizeof(T5AIMailboxMessage)) {
    (void)t5ai_cp_mailbox_acknowledge_frame(frame, false);
    return;
  }
  T5AIMailboxMessage *message = (T5AIMailboxMessage *)(uintptr_t)frame->address;
  T5AIMbIpcSocket *socket = prv_find_receiver(message);
  if (socket == NULL) {
    prv_set_route_status(message, MB_IPC_ROUTE_UNREACHABLE);
    return;
  }

  const uint8_t command = (uint8_t)message->header;
  if ((command & MB_IPC_RESPONSE_FLAG) != 0u) {
    if (!socket->tx_in_process) {
      prv_set_route_status(message, MB_IPC_ROUTE_UNSOLICITED);
      return;
    }
    if (prv_tag(message) != socket->tx_tag ||
        (command & ~MB_IPC_RESPONSE_FLAG) != socket->tx_command) {
      prv_set_route_status(message, MB_IPC_ROUTE_UNMATCHED);
      return;
    }
    socket->tx_status = prv_api_status(message) == 0u ? S_SUCCESS : E_ERROR;
    socket->tx_in_process = false;
    (void)pbl_sem_give(&socket->tx_ready);
    prv_set_route_status(message, MB_IPC_ROUTE_OK);
    return;
  }

  if (socket->rx_in_process) {
    prv_set_route_status(message, MB_IPC_ROUTE_RX_BUSY);
    return;
  }
  socket->rx_message = *message;
  socket->rx_in_process = true;
  (void)pbl_sem_give(&socket->rx_ready);
  prv_set_route_status(message, MB_IPC_ROUTE_OK);
}

status_t t5ai_mb_ipc_init(void) {
  if (s_initialized) {
    return S_SUCCESS;
  }
  if (!s_sync_initialized) {
    for (size_t i = 0; i < MB_IPC_SOCKET_COUNT; ++i) {
      pbl_mutex_init(&s_sockets[i].mutex);
      pbl_sem_init(&s_sockets[i].tx_ready, 0, 1);
      pbl_sem_init(&s_sockets[i].rx_ready, 0, 1);
    }
    s_sync_initialized = true;
  }
  status_t status = t5ai_cp_mailbox_register(MB_IPC_RX_CHANNEL, prv_receive_callback, NULL);
  if (FAILED(status) && status != E_INVALID_OPERATION) {
    return status;
  }
  s_initialized = true;
  return S_SUCCESS;
}

static status_t prv_wait_for_logical_response(T5AIMbIpcSocket *socket, uint32_t timeout_ms) {
  if (pbl_sem_take(&socket->tx_ready, PBL_TICKS(pbl_ms_to_ticks(timeout_ms))) != 0) {
    socket->tx_in_process = false;
    socket->connected = false;
    socket->poisoned = true;
    ++socket->generation;
    return E_AGAIN;
  }
  return socket->tx_status;
}

static status_t prv_transmit(T5AIMbIpcSocket *socket, uint8_t command, uint8_t user_command,
                             const void *data, size_t length, uint32_t timeout_ms) {
  if (socket->poisoned || (data != NULL && length != 0u &&
       !t5ai_cp_mailbox_shared_range_valid((uintptr_t)data, length))) {
    return socket->poisoned ? E_INVALID_OPERATION : E_RANGE;
  }
  while (pbl_sem_take(&socket->tx_ready, PBL_TICKS(0u)) == 0) {
  }
  ++socket->tx_tag;
  socket->tx_command = command;
  socket->tx_status = E_AGAIN;
  socket->tx_in_process = true;
  socket->tx_message = (T5AIMailboxMessage){
      .header = command,
      .parameter1 = prv_address(MB_IPC_CPU_CP, socket->server_port, MB_IPC_CPU_AP,
                                socket->client_port, socket->tx_tag),
      .parameter2 = (uint32_t)length | ((uint32_t)prv_crc8(data, length) << MB_IPC_CRC_SHIFT) |
                    ((uint32_t)user_command << MB_IPC_USER_COMMAND_SHIFT),
      .parameter3 = (uint32_t)(uintptr_t)data,
  };
  if (data != NULL && length != 0u) {
    dcache_flush((void *)data, length);
  }
  T5AIMailboxMessage ack;
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  const bool trace_write_send =
      socket->server_port == 1u && command == MB_IPC_COMMAND_SEND && user_command == 5u;
  uint32_t phase_start = pbl_uptime_ticks();
#endif
  status_t status = t5ai_mailbox_channel_send(MB_IPC_TX_CHANNEL, &socket->tx_message, &ack,
                                              MB_IPC_PHYSICAL_TIMEOUT_MS);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  if (trace_write_send) {
    t5ai_io_trace_sample(T5IO_WRITE_PHYSICAL, pbl_uptime_ticks() - phase_start);
  }
#endif
  if (FAILED(status)) {
    PBL_LOG_ERR("T5AI MIPC physical send failed: status=%ld cmd=%u user=%u tag=%u port=%u>%u",
                  (long)status, command, user_command, socket->tx_tag, socket->client_port,
                  socket->server_port);
    socket->tx_in_process = false;
    return status;
  }
  const uint8_t route_status = prv_route_status(&ack);
  if (route_status != MB_IPC_ROUTE_OK) {
    PBL_LOG_ERR("T5AI MIPC route rejected: route=%u api=%u cmd=%u user=%u tag=%u port=%u>%u",
                  route_status, prv_api_status(&ack), command, user_command, socket->tx_tag,
                  socket->client_port, socket->server_port);
    socket->tx_in_process = false;
    return E_INVALID_OPERATION;
  }
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  phase_start = pbl_uptime_ticks();
#endif
  status = prv_wait_for_logical_response(socket, timeout_ms);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  if (trace_write_send) {
    t5ai_io_trace_sample(T5IO_WRITE_LOGICAL, pbl_uptime_ticks() - phase_start);
  }
#endif
  return status;
}

status_t t5ai_mb_ipc_open(uint8_t client_port, uint8_t server_port, T5AIMbIpcSocket **socket_out) {
  if (client_port < MB_IPC_CLIENT_PORT_MIN || client_port > MB_IPC_CLIENT_PORT_MAX ||
      server_port == 0u || server_port > MB_IPC_SERVER_PORT_MAX || socket_out == NULL) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = t5ai_mb_ipc_init();
  if (FAILED(status)) {
    return status;
  }
  T5AIMbIpcSocket *socket = NULL;
  for (size_t i = 0; i < MB_IPC_SOCKET_COUNT; ++i) {
    if (s_sockets[i].allocated && s_sockets[i].client_port == client_port) {
      if (s_sockets[i].server_port != server_port || s_sockets[i].poisoned) {
        return E_INVALID_OPERATION;
      }
      socket = &s_sockets[i];
      break;
    }
    if (socket == NULL && !s_sockets[i].allocated) {
      socket = &s_sockets[i];
    }
  }
  if (socket == NULL) {
    return E_OUT_OF_RESOURCES;
  }
  pbl_mutex_lock(&socket->mutex, PBL_FOREVER);
  socket->allocated = true;
  socket->connected = false;
  socket->poisoned = false;
  socket->client_port = client_port;
  socket->server_port = server_port;
  socket->rx_in_process = false;
  status = prv_transmit(socket, MB_IPC_COMMAND_CONNECT, 0u, NULL, 0u, MB_IPC_CONNECT_TIMEOUT_MS);
  socket->connected = PASSED(status);
  pbl_mutex_unlock(&socket->mutex);
  if (FAILED(status)) {
    return status;
  }
  *socket_out = socket;
  return S_SUCCESS;
}

status_t t5ai_mb_ipc_send(T5AIMbIpcSocket *socket, uint8_t command, const void *data, size_t length,
                          uint32_t timeout_ms) {
  if (socket == NULL || data == NULL || length == 0u || length > MB_IPC_MAX_PAYLOAD_BYTES) {
    return E_INVALID_ARGUMENT;
  }
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  uint32_t lock_start = pbl_uptime_ticks();
#endif
  pbl_mutex_lock(&socket->mutex, PBL_FOREVER);
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  if (socket->server_port == 1u && command == 5u) {
    t5ai_io_trace_sample(T5IO_WRITE_SOCKET_LOCK, pbl_uptime_ticks() - lock_start);
  }
#endif
  status_t status;
  if (socket->connected) {
    status = prv_transmit(socket, MB_IPC_COMMAND_SEND, command, data, length, timeout_ms);
  } else {
    PBL_LOG_ERR("T5AI MIPC send on disconnected socket: user=%u port=%u>%u generation=%lu", command,
                socket->client_port, socket->server_port, (unsigned long)socket->generation);
    status = E_INVALID_OPERATION;
  }
  pbl_mutex_unlock(&socket->mutex);
  return status;
}

static status_t prv_respond(T5AIMbIpcSocket *socket, T5AIMailboxMessage *request) {
  T5AIMailboxMessage response = *request;
  response.header = ((uint8_t)request->header) | MB_IPC_RESPONSE_FLAG;
  response.parameter1 = prv_address(MB_IPC_CPU_CP, socket->server_port, MB_IPC_CPU_AP,
                                    socket->client_port, prv_tag(request));
  T5AIMailboxMessage ack;
  status_t status =
      t5ai_mailbox_channel_send(MB_IPC_TX_CHANNEL, &response, &ack, MB_IPC_PHYSICAL_TIMEOUT_MS);
  if (FAILED(status)) {
    PBL_LOG_ERR("T5AI MIPC response physical send failed: status=%ld user=%u tag=%u port=%u>%u",
                  (long)status, (unsigned)(request->parameter2 >> MB_IPC_USER_COMMAND_SHIFT),
                  prv_tag(request), socket->client_port, socket->server_port);
  } else if (prv_route_status(&ack) != MB_IPC_ROUTE_OK) {
    PBL_LOG_WRN("T5AI MIPC response route rejected: route=%u api=%u user=%u tag=%u port=%u>%u",
                  prv_route_status(&ack), prv_api_status(&ack),
                  (unsigned)(request->parameter2 >> MB_IPC_USER_COMMAND_SHIFT), prv_tag(request),
                  socket->client_port, socket->server_port);
    status = E_INVALID_OPERATION;
  }
  return status;
}

status_t t5ai_mb_ipc_receive(T5AIMbIpcSocket *socket, uint8_t *command, void *data, size_t length,
                             uint32_t timeout_ms) {
  if (socket == NULL) {
    return E_INVALID_ARGUMENT;
  }
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  uint32_t wait_start = pbl_uptime_ticks();
#endif
  if (socket->rx_in_process) {
    (void)pbl_sem_take(&socket->rx_ready, PBL_TICKS(0u));
  } else if (pbl_sem_take(&socket->rx_ready, PBL_TICKS(pbl_ms_to_ticks(timeout_ms))) != 0) {
    /* The CP may still publish a completion which references the caller's
     * transaction storage. Do not permit that storage to be reused. */
    socket->connected = false;
    socket->poisoned = true;
    ++socket->generation;
    return E_AGAIN;
  }
  if (!socket->rx_in_process) {
    return E_AGAIN;
  }
  T5AIMailboxMessage request = socket->rx_message;
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  const bool trace_write =
      socket->server_port == 1u && (uint8_t)(request.parameter2 >> MB_IPC_USER_COMMAND_SHIFT) == 5u;
  if (trace_write) {
    t5ai_io_trace_sample(T5IO_WRITE_WAIT, pbl_uptime_ticks() - wait_start);
  }
#endif
  const size_t received_length = (uint16_t)request.parameter2;
  status_t status = S_SUCCESS;
  if ((uint8_t)request.header != MB_IPC_COMMAND_SEND || request.parameter3 == 0u ||
      !t5ai_cp_mailbox_shared_range_valid((uintptr_t)request.parameter3, received_length) ||
      (data != NULL && received_length != length)) {
    status = E_ERROR;
  } else if (data != NULL && length != 0u) {
    const void *source = (const void *)(uintptr_t)request.parameter3;
    dcache_invalidate((void *)source, received_length);
    if (prv_crc8(source, received_length) != (uint8_t)(request.parameter2 >> MB_IPC_CRC_SHIFT)) {
      status = E_ERROR;
    } else {
      memcpy(data, source, received_length);
    }
  }
  if (command != NULL) {
    *command = (uint8_t)(request.parameter2 >> MB_IPC_USER_COMMAND_SHIFT);
  }
  /* Match Tuya's mb_ipc_recv_async semantics: once the request payload has
   * been received and validated, the response route status is diagnostic
   * only. A delayed response can be rejected as stale after the CP has already
   * completed the flash operation; propagating that rejection turns a
   * successful write into a fatal Pebble flash error. */
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  uint32_t ack_start = pbl_uptime_ticks();
#endif
  socket->rx_ack_aon = T5AI_AON_RTC_COUNT_LOW;
  const status_t ack_status = prv_respond(socket, &request);
  const uint8_t user_command = (uint8_t)(request.parameter2 >> MB_IPC_USER_COMMAND_SHIFT);
  // A READ lease cannot be trusted after a rejected/late acknowledgement.
  // Keep the vendor completion semantics for already completed mutations.
  if (socket->server_port == 1u && (user_command == 3u || user_command == 4u) &&
      FAILED(ack_status)) {
    status = ack_status;
  }
#ifdef CONFIG_T5AI_WDT_RECOVERY_TRACE
  if (trace_write) {
    t5ai_io_trace_sample(T5IO_WRITE_ACK, pbl_uptime_ticks() - ack_start);
  }
#endif
  socket->rx_in_process = false;
  return status;
}

uint32_t t5ai_mb_ipc_last_receive_aon(const T5AIMbIpcSocket *socket) {
  return socket->rx_ack_aon;
}

void t5ai_mb_ipc_discard(T5AIMbIpcSocket *socket) {
  if (socket == NULL) {
    return;
  }
  if (socket->rx_in_process) {
    T5AIMailboxMessage request = socket->rx_message;
    (void)prv_respond(socket, &request);
    socket->rx_in_process = false;
  }
  while (pbl_sem_take(&socket->rx_ready, PBL_TICKS(0u)) == 0) {
  }
}

void t5ai_mb_ipc_poison(T5AIMbIpcSocket *socket) {
  if (socket != NULL) {
    socket->connected = false;
    socket->poisoned = true;
    socket->tx_in_process = false;
    socket->rx_in_process = false;
    ++socket->generation;
  }
}
