/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/flash_ipc.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "system/status_codes.h"

#define T5AI_IPC_TX_LOGICAL_CHANNEL 0x11u
#define T5AI_IPC_RX_LOGICAL_CHANNEL 0x41u
#define T5AI_SARADC_NOTIFY_RX_LOGICAL_CHANNEL 0x4cu
#define T5AI_IPC_ACK_CONTROL 0x1u
#define T5AI_IPC_COMMAND_CONNECT 0u
#define T5AI_IPC_COMMAND_SEND 2u
#define T5AI_IPC_RESPONSE_FLAG 0x80u
#define T5AI_IPC_ROUTE_UNREACHABLE 2u
#define T5AI_FLASH_CLIENT_PORT 16u
#define T5AI_FLASH_SERVER_PORT 1u
#define T5AI_SARADC_CLIENT_PORT 17u
#define T5AI_SARADC_SERVER_PORT 2u
#define T5AI_SARADC_NOTIFY_ACK 2u
#define T5AI_IPC_PENDING_CAPACITY 4u
#define T5AI_IPC_ACK_CAPACITY 8u

#define T5AI_FLASH_COMMAND_ERASE_SECTOR 0u
#define T5AI_FLASH_COMMAND_READ 3u
#define T5AI_FLASH_COMMAND_READ_DONE 4u
#define T5AI_FLASH_COMMAND_WRITE 5u

typedef struct {
  uint32_t header;
  uint32_t address;
  uint32_t data;
  uint32_t buffer;
} T5AIIPCMessage;

typedef struct {
  uint32_t part_and_address;
  uint32_t buffer;
  uint32_t length_and_status;
  uint32_t crc;
} T5AIFlashCommand;

static T5AIIPCMessage s_tx_message __attribute__((aligned(32)));
static T5AIIPCMessage s_ack_messages[T5AI_IPC_ACK_CAPACITY] __attribute__((aligned(32)));
static T5AIIPCMessage s_saradc_notify_acks[T5AI_IPC_ACK_CAPACITY] __attribute__((aligned(32)));
static T5AIIPCMessage s_pending_messages[T5AI_IPC_PENDING_CAPACITY];
static T5AIFlashCommand s_flash_command __attribute__((aligned(32)));
static uint8_t s_pending_head;
static uint8_t s_pending_count;
static uint8_t s_ack_index;
static uint8_t s_saradc_notify_ack_index;
static uint8_t s_tag;
static uint8_t s_sequence;
static bool s_connected;

static uint8_t prv_crc8(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint8_t crc = 0;
  while (length-- != 0) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static uint32_t prv_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0;
  while (length-- != 0) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
  }
  return crc;
}

static uint32_t prv_saradc_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0;
  while (length-- != 0) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80u) ? (crc << 1) ^ 0x31u : crc << 1;
    }
  }
  return crc;
}

static uint32_t prv_ipc_address_word_for_ports(uint8_t tag, uint8_t client_port,
                                               uint8_t server_port) {
  return server_port | ((uint32_t)client_port << 8) | ((uint32_t)1u << 14) | ((uint32_t)tag << 16);
}

static uint32_t __attribute__((unused)) prv_ipc_address_word(uint8_t tag) {
  return prv_ipc_address_word_for_ports(tag, T5AI_FLASH_CLIENT_PORT, T5AI_FLASH_SERVER_PORT);
}

static uint32_t prv_ipc_data_word(uint8_t user_command, size_t length, uint8_t crc) {
  return (uint32_t)length | ((uint32_t)crc << 16) | ((uint32_t)user_command << 24);
}

static status_t prv_mailbox_send(T5AIIPCMessage *message, bool acknowledge) {
  if (!acknowledge) {
    s_sequence++;
    message->header = (message->header & 0xffu) | ((uint32_t)s_sequence << 16) |
                      ((uint32_t)T5AI_IPC_TX_LOGICAL_CHANNEL << 24);
  }
  dcache_flush(message, sizeof(*message));
  return t5ai_cp_mailbox_send(message, sizeof(*message));
}

static status_t prv_acknowledge(const T5AIIPCMessage *received);

static status_t prv_pending_enqueue(const T5AIIPCMessage *message) {
  if (s_pending_count == T5AI_IPC_PENDING_CAPACITY) {
    return E_OUT_OF_RESOURCES;
  }
  const uint8_t index = (uint8_t)((s_pending_head + s_pending_count) % T5AI_IPC_PENDING_CAPACITY);
  s_pending_messages[index] = *message;
  ++s_pending_count;
  return S_SUCCESS;
}

static bool prv_pending_dequeue(T5AIIPCMessage *message) {
  if (s_pending_count == 0u) {
    return false;
  }
  *message = s_pending_messages[s_pending_head];
  s_pending_head = (uint8_t)((s_pending_head + 1u) % T5AI_IPC_PENDING_CAPACITY);
  --s_pending_count;
  return true;
}

static status_t prv_mailbox_receive(uint8_t logical_channel, T5AIIPCMessage *message) {
  T5AICPMailboxFrame frame;
  status_t status = t5ai_cp_mailbox_receive(logical_channel, &frame);
  if (FAILED(status)) {
    return status;
  }
  if (frame.length != sizeof(*message)) {
    return E_ERROR;
  }
  dcache_invalidate((void *)frame.address, sizeof(*message));
  memcpy(message, (const void *)frame.address, sizeof(*message));
  return S_SUCCESS;
}

static void prv_ipc_receive_callback(uint8_t logical_channel, const T5AICPMailboxFrame *frame,
                                     void *context) {
  (void)logical_channel;
  (void)context;
  if (frame->length != sizeof(T5AIIPCMessage) || s_pending_count == T5AI_IPC_PENDING_CAPACITY) {
    return;
  }
  T5AIIPCMessage message;
  dcache_invalidate((void *)frame->address, sizeof(message));
  memcpy(&message, (const void *)frame->address, sizeof(message));
  if (((message.header >> 12u) & T5AI_IPC_ACK_CONTROL) != 0u) {
    return;
  }
  if (PASSED(prv_acknowledge(&message))) {
    (void)prv_pending_enqueue(&message);
  }
}

static status_t prv_acknowledge(const T5AIIPCMessage *received) {
  T5AIIPCMessage *ack = &s_ack_messages[s_ack_index];
  s_ack_index = (uint8_t)((s_ack_index + 1u) % T5AI_IPC_ACK_CAPACITY);
  *ack = *received;
  ack->header = (ack->header & ~(0xfu << 8u)) | ((uint32_t)T5AI_IPC_ACK_CONTROL << 12);
  return prv_mailbox_send(ack, true);
}

static void prv_saradc_notify_callback(uint8_t logical_channel, const T5AICPMailboxFrame *frame,
                                       void *context) {
  (void)logical_channel;
  (void)context;
  if (frame->length < sizeof(T5AIIPCMessage)) {
    return;
  }

  T5AIIPCMessage *ack = &s_saradc_notify_acks[s_saradc_notify_ack_index];
  s_saradc_notify_ack_index = (uint8_t)((s_saradc_notify_ack_index + 1u) % T5AI_IPC_ACK_CAPACITY);
  dcache_invalidate((void *)frame->address, sizeof(*ack));
  memcpy(ack, (const void *)frame->address, sizeof(*ack));
  ack->header = (ack->header & ~(0xfu << 8u)) | ((uint32_t)T5AI_IPC_ACK_CONTROL << 12);
  ack->address = T5AI_SARADC_NOTIFY_ACK;
  (void)prv_mailbox_send(ack, true);
}

static status_t prv_ipc_ack_status(const T5AIIPCMessage *ack) {
  if ((ack->header & (0xfu << 8u)) != 0u) {
    return E_ERROR;
  }
  const uint8_t route_status = (uint8_t)(ack->address >> 28u);
  if (route_status == T5AI_IPC_ROUTE_UNREACHABLE) {
    return E_AGAIN;
  }
  return route_status == 0u ? S_SUCCESS : E_ERROR;
}

static status_t prv_send_and_wait_for_ack(T5AIIPCMessage *message) {
  status_t status = prv_mailbox_send(message, false);
  if (FAILED(status)) {
    return status;
  }

  T5AIIPCMessage received;
  for (;;) {
    status = prv_mailbox_receive(T5AI_IPC_TX_LOGICAL_CHANNEL, &received);
    if (FAILED(status)) {
      return status;
    }
    if (((received.header >> 12) & T5AI_IPC_ACK_CONTROL) != 0) {
      if ((uint8_t)(received.header >> 16) == s_sequence) {
        return prv_ipc_ack_status(&received);
      }
      continue;
    }
    status = prv_acknowledge(&received);
    if (FAILED(status)) {
      return status;
    }
    /* The peer can queue its logical response before the physical transport
     * ACK. Preserve that response for the receive half of the transaction. */
    status = prv_pending_enqueue(&received);
    if (FAILED(status)) {
      return status;
    }
  }
}

static status_t prv_wait_for_ipc(uint8_t command, T5AIIPCMessage *received) {
  for (;;) {
    if (!prv_pending_dequeue(received)) {
      status_t status = prv_mailbox_receive(T5AI_IPC_RX_LOGICAL_CHANNEL, received);
      if (FAILED(status)) {
        return status;
      }
      if (((received->header >> 12) & T5AI_IPC_ACK_CONTROL) != 0) {
        continue;
      }
      status = prv_acknowledge(received);
      if (FAILED(status)) {
        return status;
      }
    }
    if ((uint8_t)received->header == command) {
      return S_SUCCESS;
    }
  }
}

static status_t prv_wait_for_ipc_transaction(uint8_t command, uint8_t tag,
                                             T5AIIPCMessage *received) {
  for (;;) {
    status_t status = prv_wait_for_ipc(command, received);
    if (FAILED(status) || (uint8_t)(received->address >> 16u) == tag) {
      return status;
    }
  }
}

static status_t prv_send_ipc_response(const T5AIIPCMessage *request, uint8_t client_port,
                                      uint8_t server_port) {
  s_tx_message = *request;
  s_tx_message.header = T5AI_IPC_COMMAND_SEND | T5AI_IPC_RESPONSE_FLAG;
  s_tx_message.address =
      prv_ipc_address_word_for_ports((uint8_t)(request->address >> 16), client_port, server_port);
  return prv_send_and_wait_for_ack(&s_tx_message);
}

static status_t prv_ipc_send(uint8_t user_command, const void *data, size_t length,
                             uint8_t client_port, uint8_t server_port) {
  s_tag++;
  s_tx_message = (T5AIIPCMessage){
      .header = T5AI_IPC_COMMAND_SEND,
      .address = prv_ipc_address_word_for_ports(s_tag, client_port, server_port),
      .data = prv_ipc_data_word(user_command, length, prv_crc8(data, length)),
      .buffer = (uint32_t)(uintptr_t)data,
  };
  dcache_flush(data, length);
  status_t status = prv_send_and_wait_for_ack(&s_tx_message);
  if (FAILED(status)) {
    return status;
  }

  T5AIIPCMessage response;
  status = prv_wait_for_ipc_transaction(T5AI_IPC_COMMAND_SEND | T5AI_IPC_RESPONSE_FLAG, s_tag,
                                        &response);
  if (FAILED(status)) {
    return status;
  }
  return (response.address >> 24u) == 0u ? S_SUCCESS : E_ERROR;
}

static status_t prv_ipc_receive(uint8_t user_command, void *data, size_t length,
                                uint8_t client_port, uint8_t server_port) {
  T5AIIPCMessage request;
  status_t status = prv_wait_for_ipc(T5AI_IPC_COMMAND_SEND, &request);
  if (FAILED(status) || (uint8_t)(request.data >> 24) != user_command ||
      (uint16_t)request.data != length || request.buffer == 0) {
    return E_ERROR;
  }

  const void *source = (const void *)(uintptr_t)request.buffer;
  dcache_invalidate((void *)source, length);
  if (prv_crc8(source, length) != (uint8_t)(request.data >> 16)) {
    return E_ERROR;
  }
  memcpy(data, source, length);
  return prv_send_ipc_response(&request, client_port, server_port);
}

static status_t prv_connect(uint8_t client_port, uint8_t server_port) {
  s_tag++;
  s_tx_message = (T5AIIPCMessage){
      .header = T5AI_IPC_COMMAND_CONNECT,
      .address = prv_ipc_address_word_for_ports(s_tag, client_port, server_port),
  };
  status_t status = prv_send_and_wait_for_ack(&s_tx_message);
  if (FAILED(status)) {
    return status;
  }
  T5AIIPCMessage response;
  /* A timed-out CONNECT can still complete at the peer. The socket identity
   * is the fixed port pair, so a successful late CONNECT response proves the
   * same connection and must not be rejected solely for its earlier tag. */
  status = prv_wait_for_ipc(T5AI_IPC_COMMAND_CONNECT | T5AI_IPC_RESPONSE_FLAG, &response);
  if (FAILED(status)) {
    return status;
  }
  return (response.address >> 24u) == 0u ? S_SUCCESS : E_ERROR;
}

status_t t5ai_flash_ipc_init(void) {
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  status = t5ai_cp_mailbox_register(T5AI_IPC_RX_LOGICAL_CHANNEL, prv_ipc_receive_callback, NULL);
  if (FAILED(status) && status != E_INVALID_OPERATION) {
    t5ai_cp_mailbox_unlock();
    return status;
  }
  status = t5ai_cp_mailbox_register(T5AI_SARADC_NOTIFY_RX_LOGICAL_CHANNEL,
                                    prv_saradc_notify_callback, NULL);
  if (FAILED(status) && status != E_INVALID_OPERATION) {
    t5ai_cp_mailbox_unlock();
    return status;
  }
  status = prv_connect(T5AI_FLASH_CLIENT_PORT, T5AI_FLASH_SERVER_PORT);
  s_connected = PASSED(status);
  t5ai_cp_mailbox_unlock();
  return s_connected ? S_SUCCESS : E_ERROR;
}

static status_t prv_flash_transaction(uint8_t command) {
  if (!s_connected) {
    return E_INVALID_OPERATION;
  }
  status_t status = prv_ipc_send(command, &s_flash_command, sizeof(s_flash_command),
                                 T5AI_FLASH_CLIENT_PORT, T5AI_FLASH_SERVER_PORT);
  if (FAILED(status)) {
    return status;
  }
  status = prv_ipc_receive(command, &s_flash_command, sizeof(s_flash_command),
                           T5AI_FLASH_CLIENT_PORT, T5AI_FLASH_SERVER_PORT);
  return FAILED(status) || (int16_t)(s_flash_command.length_and_status >> 16) != 0 ? E_ERROR
                                                                                   : S_SUCCESS;
}

status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  if (buffer == NULL || length == 0 || length > 0xffffu) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  s_flash_command = (T5AIFlashCommand){
      .part_and_address = offset << 8,
      .length_and_status = (uint32_t)length,
  };
  status = prv_flash_transaction(T5AI_FLASH_COMMAND_READ);
  if (FAILED(status) || s_flash_command.buffer == 0) {
    status = E_ERROR;
    goto out;
  }
  const void *source = (const void *)(uintptr_t)s_flash_command.buffer;
  dcache_invalidate((void *)source, length);
  if (prv_crc32(source, length) != s_flash_command.crc) {
    status = E_ERROR;
    goto out;
  }
  memcpy(buffer, source, length);

  memset(&s_flash_command, 0, sizeof(s_flash_command));
  status = prv_flash_transaction(T5AI_FLASH_COMMAND_READ_DONE);
out:
  t5ai_cp_mailbox_unlock();
  return status;
}

status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length) {
  if (buffer == NULL || length == 0 || length > 0xffffu) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  s_flash_command = (T5AIFlashCommand){
      .part_and_address = offset << 8,
      .buffer = (uint32_t)(uintptr_t)buffer,
      .length_and_status = (uint32_t)length,
      .crc = prv_crc32(buffer, length),
  };
  dcache_flush(buffer, length);
  status = prv_flash_transaction(T5AI_FLASH_COMMAND_WRITE);
  t5ai_cp_mailbox_unlock();
  return status;
}

status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  if ((offset & 0xfffu) != 0) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  s_flash_command = (T5AIFlashCommand){.part_and_address = offset << 8};
  status = prv_flash_transaction(T5AI_FLASH_COMMAND_ERASE_SECTOR);
  t5ai_cp_mailbox_unlock();
  return status;
}

typedef struct {
  uint32_t clock;
  uint32_t sample_rate;
  uint32_t filter;
  uint32_t steady;
  uint8_t mode;
  uint8_t source_clock;
  uint8_t channel;
  uint8_t saturate_mode;
  uint32_t is_open;
  uint16_t *output_buffer;
  int32_t output_length;
  uint16_t use_hardware_calibration;
  uint8_t voltage_divider;
} T5AISaradcConfig;

typedef struct {
  uint32_t parameter;
  uint16_t samples[32];
  uint32_t size;
  uint32_t timeout;
  int16_t status;
  uint8_t sample_count;
  uint16_t calibration_data;
  uint32_t crc;
  T5AISaradcConfig config;
  void *callback;
  void *context;
} T5AISaradcCommand;

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(T5AISaradcConfig) == 36u, "BK7258 SARADC config ABI changed");
_Static_assert(sizeof(T5AISaradcCommand) == 132u, "BK7258 SARADC mailbox ABI changed");
#endif

enum {
  T5AI_SARADC_ACQUIRE = 0,
  T5AI_SARADC_INIT = 1,
  T5AI_SARADC_ENABLE_BYPASS_CALIBRATION = 2,
  T5AI_SARADC_START = 3,
  T5AI_SARADC_READ_RAW = 4,
  T5AI_SARADC_READ_RAW_DONE = 5,
  T5AI_SARADC_STOP = 6,
  T5AI_SARADC_DEINIT = 7,
  T5AI_SARADC_RELEASE = 8,
  T5AI_SARADC_SET_CONFIG = 9,
  T5AI_SARADC_INIT_GPIO = 29,
  T5AI_SARADC_DEINIT_GPIO = 30,
};

static T5AISaradcCommand s_saradc_command __attribute__((aligned(32)));
static bool s_saradc_connected;

static status_t prv_saradc_command(uint8_t command) {
  status_t status = prv_ipc_send(command, &s_saradc_command, sizeof(s_saradc_command),
                                 T5AI_SARADC_CLIENT_PORT, T5AI_SARADC_SERVER_PORT);
  if (FAILED(status)) {
    return status;
  }
  status = prv_ipc_receive(command, &s_saradc_command, sizeof(s_saradc_command),
                           T5AI_SARADC_CLIENT_PORT, T5AI_SARADC_SERVER_PORT);
  if (FAILED(status)) {
    return status;
  }
  if (s_saradc_command.status != 0) {
    return E_ERROR;
  }
  return S_SUCCESS;
}

static status_t prv_saradc_cleanup(uint8_t channel, status_t status) {
  memset(&s_saradc_command, 0, sizeof(s_saradc_command));
  (void)prv_saradc_command(T5AI_SARADC_STOP);
  s_saradc_command.config.channel = channel;
  (void)prv_saradc_command(T5AI_SARADC_DEINIT);
  (void)prv_saradc_command(T5AI_SARADC_RELEASE);
  s_saradc_command.config.channel = channel;
  (void)prv_saradc_command(T5AI_SARADC_DEINIT_GPIO);
  return status;
}

static status_t prv_saradc_read_peak_locked(uint8_t channel, uint8_t samples, uint16_t *peak) {
  if (!s_saradc_connected) {
    status_t status = prv_connect(T5AI_SARADC_CLIENT_PORT, T5AI_SARADC_SERVER_PORT);
    if (FAILED(status)) {
      return status;
    }
    s_saradc_connected = true;
  }

  memset(&s_saradc_command, 0, sizeof(s_saradc_command));
  s_saradc_command.config.channel = channel;
  status_t status = prv_saradc_command(T5AI_SARADC_INIT_GPIO);
  if (FAILED(status)) {
    return status;
  }
  status = prv_saradc_command(T5AI_SARADC_ACQUIRE);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_command(T5AI_SARADC_INIT);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }

  memset(&s_saradc_command.config, 0, sizeof(s_saradc_command.config));
  s_saradc_command.config.clock = 0x30a0c5u;
  s_saradc_command.config.sample_rate = 0u;
  s_saradc_command.config.mode = 3u;
  s_saradc_command.config.source_clock = 1u;
  s_saradc_command.config.channel = channel;
  s_saradc_command.config.saturate_mode = 4u;
  s_saradc_command.config.steady = 7u;
  s_saradc_command.config.output_buffer = s_saradc_command.samples;
  s_saradc_command.config.output_length = samples;
  status = prv_saradc_command(T5AI_SARADC_SET_CONFIG);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  status = prv_saradc_command(T5AI_SARADC_ENABLE_BYPASS_CALIBRATION);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  status = prv_saradc_command(T5AI_SARADC_START);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }

  memset(s_saradc_command.samples, 0, sizeof(s_saradc_command.samples));
  s_saradc_command.size = samples;
  s_saradc_command.timeout = UINT32_MAX;
  status = prv_saradc_command(T5AI_SARADC_READ_RAW);
  if (PASSED(status) && s_saradc_command.size == samples &&
      prv_saradc_crc32(s_saradc_command.samples, samples * sizeof(uint16_t)) ==
          s_saradc_command.crc) {
    uint16_t maximum = 0;
    for (uint8_t i = 0; i < samples; ++i) {
      if (s_saradc_command.samples[i] > maximum) {
        maximum = s_saradc_command.samples[i];
      }
    }
    *peak = maximum;
    status = prv_saradc_command(T5AI_SARADC_READ_RAW_DONE);
  } else {
    status = E_ERROR;
  }
  return prv_saradc_cleanup(channel, status);
}

status_t t5ai_saradc_read_peak(uint8_t channel, uint8_t samples, uint16_t *peak) {
  if (peak == NULL || channel > 15u || samples == 0u || samples > 32u) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  status = prv_saradc_read_peak_locked(channel, samples, peak);
  t5ai_cp_mailbox_unlock();
  return status;
}
