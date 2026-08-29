/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/flash_ipc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "system/status_codes.h"

/* BK7258 AP mailbox channel 1 registers from the pinned T5AI BSP. */
#define T5AI_MBOX_BASE 0x41000000u
#define T5AI_MBOX_CH1_CTRL (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x80u))
#define T5AI_MBOX_CH1_TX_DATA0 (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x88u))
#define T5AI_MBOX_CH1_TX_DATA1 (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x8cu))
#define T5AI_MBOX_CH1_TX_ID (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x90u))
#define T5AI_MBOX_CH1_RX_ID (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x94u))
#define T5AI_MBOX_CH1_RX_DATA0 (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x98u))
#define T5AI_MBOX_CH1_RX_DATA1 (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x9cu))
#define T5AI_MBOX_CH0_FIFO_STATUS (*(volatile uint32_t *)(T5AI_MBOX_BASE + 0x60u))

#define T5AI_MBOX_FIFO_NOT_EMPTY (1u << 19)
#define T5AI_MBOX_FIFO_FULL (1u << 0)
#define T5AI_MBOX_CPU_CP 0u

#define T5AI_IPC_LOGICAL_CHANNEL 0x11u
#define T5AI_IPC_ACK_CONTROL 0x1u
#define T5AI_IPC_COMMAND_CONNECT 0u
#define T5AI_IPC_COMMAND_SEND 2u
#define T5AI_IPC_RESPONSE_FLAG 0x80u
#define T5AI_IPC_CLIENT_PORT 16u
#define T5AI_IPC_SERVER_PORT 1u
#define T5AI_IPC_INVALID_USER_COMMAND 0xffu

#define T5AI_FLASH_COMMAND_ERASE_SECTOR 0u
#define T5AI_FLASH_COMMAND_READ 3u
#define T5AI_FLASH_COMMAND_READ_DONE 4u
#define T5AI_FLASH_COMMAND_WRITE 5u

#define T5AI_AON_RTC_LOW (*(volatile uint32_t *)0x4400020cu)
#define T5AI_AON_RTC_HIGH (*(volatile uint32_t *)0x44000228u)
#define T5AI_AON_RTC_HZ 32000u
#define T5AI_IPC_TIMEOUT_TICKS (2u * T5AI_AON_RTC_HZ)

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
static T5AIIPCMessage s_ack_message __attribute__((aligned(32)));
static T5AIFlashCommand s_flash_command __attribute__((aligned(32)));
static uint8_t s_tag;
static uint8_t s_sequence;
static bool s_connected;

static uint64_t prv_ticks(void) {
  uint32_t high_before;
  uint32_t low;
  uint32_t high_after;
  do {
    high_before = T5AI_AON_RTC_HIGH;
    low = T5AI_AON_RTC_LOW;
    high_after = T5AI_AON_RTC_HIGH;
  } while (high_before != high_after);
  return ((uint64_t)high_after << 32) | low;
}

static bool prv_timed_out(uint64_t start) {
  return prv_ticks() - start >= T5AI_IPC_TIMEOUT_TICKS;
}

static uint8_t prv_crc8(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint8_t crc = 0;
  while (length-- != 0) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                          : (uint8_t)(crc << 1);
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

static uint32_t prv_ipc_address_word(uint8_t tag) {
  return T5AI_IPC_SERVER_PORT | (T5AI_IPC_CLIENT_PORT << 8) |
         ((uint32_t)1u << 14) | ((uint32_t)tag << 16);
}

static uint32_t prv_ipc_data_word(uint8_t user_command, size_t length,
                                  uint8_t crc) {
  return (uint32_t)length | ((uint32_t)crc << 16) |
         ((uint32_t)user_command << 24);
}

static status_t prv_mailbox_send(T5AIIPCMessage *message, bool acknowledge) {
  const uint64_t start = prv_ticks();
  while ((T5AI_MBOX_CH0_FIFO_STATUS & T5AI_MBOX_FIFO_FULL) != 0) {
    if (prv_timed_out(start)) {
      return E_BUSY;
    }
  }

  if (!acknowledge) {
    s_sequence++;
    message->header = (message->header & 0xffu) |
                      ((uint32_t)s_sequence << 16) |
                      ((uint32_t)T5AI_IPC_LOGICAL_CHANNEL << 24);
  }
  dcache_flush(message, sizeof(*message));
  T5AI_MBOX_CH1_TX_DATA0 = (uint32_t)(uintptr_t)message;
  T5AI_MBOX_CH1_TX_DATA1 = sizeof(*message);
  T5AI_MBOX_CH1_TX_ID = T5AI_MBOX_CPU_CP;
  return S_SUCCESS;
}

static status_t prv_mailbox_receive(T5AIIPCMessage *message) {
  const uint64_t start = prv_ticks();
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) == 0) {
    if (prv_timed_out(start)) {
      return E_AGAIN;
    }
  }

  (void)T5AI_MBOX_CH1_RX_ID;
  const uintptr_t address = T5AI_MBOX_CH1_RX_DATA0;
  const uint32_t length = T5AI_MBOX_CH1_RX_DATA1;
  if (address == 0 || length != sizeof(*message)) {
    return E_ERROR;
  }
  dcache_invalidate((void *)address, sizeof(*message));
  memcpy(message, (const void *)address, sizeof(*message));
  return S_SUCCESS;
}

static status_t prv_acknowledge(const T5AIIPCMessage *received) {
  s_ack_message = *received;
  s_ack_message.header |= (uint32_t)T5AI_IPC_ACK_CONTROL << 12;
  return prv_mailbox_send(&s_ack_message, true);
}

static status_t prv_send_and_wait_for_ack(T5AIIPCMessage *message) {
  status_t status = prv_mailbox_send(message, false);
  if (FAILED(status)) {
    return status;
  }

  T5AIIPCMessage received;
  for (;;) {
    status = prv_mailbox_receive(&received);
    if (FAILED(status)) {
      return status;
    }
    if (((received.header >> 12) & T5AI_IPC_ACK_CONTROL) != 0 &&
        (uint8_t)(received.header >> 16) == s_sequence) {
      return (received.header & (0xfu << 8)) == 0 ? S_SUCCESS : E_ERROR;
    }
    status = prv_acknowledge(&received);
    if (FAILED(status)) {
      return status;
    }
  }
}

static status_t prv_wait_for_ipc(uint8_t command, T5AIIPCMessage *received) {
  for (;;) {
    status_t status = prv_mailbox_receive(received);
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
    if ((uint8_t)received->header == command) {
      return S_SUCCESS;
    }
  }
}

static status_t prv_send_ipc_response(const T5AIIPCMessage *request) {
  s_tx_message = *request;
  s_tx_message.header = T5AI_IPC_COMMAND_SEND | T5AI_IPC_RESPONSE_FLAG;
  s_tx_message.address = prv_ipc_address_word((uint8_t)(request->address >> 16));
  return prv_send_and_wait_for_ack(&s_tx_message);
}

static status_t prv_ipc_send(uint8_t user_command, const void *data,
                             size_t length) {
  s_tag++;
  s_tx_message = (T5AIIPCMessage){
      .header = T5AI_IPC_COMMAND_SEND,
      .address = prv_ipc_address_word(s_tag),
      .data = prv_ipc_data_word(user_command, length, prv_crc8(data, length)),
      .buffer = (uint32_t)(uintptr_t)data,
  };
  dcache_flush(data, length);
  status_t status = prv_send_and_wait_for_ack(&s_tx_message);
  if (FAILED(status)) {
    return status;
  }

  T5AIIPCMessage response;
  status = prv_wait_for_ipc(T5AI_IPC_COMMAND_SEND | T5AI_IPC_RESPONSE_FLAG,
                            &response);
  if (FAILED(status) || (uint8_t)(response.address >> 16) != s_tag) {
    return E_ERROR;
  }
  return (response.address >> 28) == 0 ? S_SUCCESS : E_ERROR;
}

static status_t prv_ipc_receive(uint8_t user_command, void *data,
                                size_t length) {
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
  return prv_send_ipc_response(&request);
}

status_t t5ai_flash_ipc_init(void) {
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) != 0) {
    (void)T5AI_MBOX_CH1_RX_ID;
    (void)T5AI_MBOX_CH1_RX_DATA0;
    (void)T5AI_MBOX_CH1_RX_DATA1;
  }

  s_tag++;
  s_tx_message = (T5AIIPCMessage){
      .header = T5AI_IPC_COMMAND_CONNECT,
      .address = prv_ipc_address_word(s_tag),
  };
  status_t status = prv_send_and_wait_for_ack(&s_tx_message);
  if (FAILED(status)) {
    return status;
  }
  T5AIIPCMessage response;
  status = prv_wait_for_ipc(T5AI_IPC_COMMAND_CONNECT | T5AI_IPC_RESPONSE_FLAG,
                            &response);
  s_connected = PASSED(status) && (response.address >> 24) == 0;
  return s_connected ? S_SUCCESS : E_ERROR;
}

static status_t prv_flash_transaction(uint8_t command) {
  if (!s_connected) {
    return E_INVALID_OPERATION;
  }
  status_t status = prv_ipc_send(command, &s_flash_command,
                                 sizeof(s_flash_command));
  if (FAILED(status)) {
    return status;
  }
  status = prv_ipc_receive(command, &s_flash_command, sizeof(s_flash_command));
  return FAILED(status) || (int16_t)(s_flash_command.length_and_status >> 16) != 0
             ? E_ERROR
             : S_SUCCESS;
}

status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  if (buffer == NULL || length == 0 || length > 0xffffu) {
    return E_INVALID_ARGUMENT;
  }
  s_flash_command = (T5AIFlashCommand){
      .part_and_address = offset << 8,
      .length_and_status = (uint32_t)length,
  };
  status_t status = prv_flash_transaction(T5AI_FLASH_COMMAND_READ);
  if (FAILED(status) || s_flash_command.buffer == 0) {
    return E_ERROR;
  }
  const void *source = (const void *)(uintptr_t)s_flash_command.buffer;
  dcache_invalidate((void *)source, length);
  if (prv_crc32(source, length) != s_flash_command.crc) {
    return E_ERROR;
  }
  memcpy(buffer, source, length);

  memset(&s_flash_command, 0, sizeof(s_flash_command));
  status = prv_flash_transaction(T5AI_FLASH_COMMAND_READ_DONE);
  return status;
}

status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer,
                              size_t length) {
  if (buffer == NULL || length == 0 || length > 0xffffu) {
    return E_INVALID_ARGUMENT;
  }
  s_flash_command = (T5AIFlashCommand){
      .part_and_address = offset << 8,
      .buffer = (uint32_t)(uintptr_t)buffer,
      .length_and_status = (uint32_t)length,
      .crc = prv_crc32(buffer, length),
  };
  dcache_flush(buffer, length);
  return prv_flash_transaction(T5AI_FLASH_COMMAND_WRITE);
}

status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  if ((offset & 0xfffu) != 0) {
    return E_INVALID_ARGUMENT;
  }
  s_flash_command = (T5AIFlashCommand){.part_and_address = offset << 8};
  return prv_flash_transaction(T5AI_FLASH_COMMAND_ERASE_SECTOR);
}
