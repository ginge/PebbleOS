/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/mcu/cache.h>
#include <pbl/logging/logging.h>
#include <pbl/os/mutex.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/flash_ipc.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

PBL_LOG_MODULE_DECLARE(driver_flash, CONFIG_DRIVER_FLASH_LOG_LEVEL);

#define FLASH_CLIENT_PORT 16u
#define FLASH_SERVER_PORT 1u
#define SARADC_CLIENT_PORT 17u
#define SARADC_SERVER_PORT 2u
#define IPC_TIMEOUT_MS 5000u
#define SARADC_IPC_TIMEOUT_MS 2000u
#define IPC_STALE_RESPONSE_LIMIT 4u
#define SARADC_NOTIFY_RX_CHANNEL 0x4cu
#define SARADC_NOTIFY_ACK 2u

enum {
  FLASH_ERASE_SECTOR = 0,
  FLASH_READ = 3,
  FLASH_READ_DONE = 4,
  FLASH_WRITE = 5,
};

typedef struct {
  uint32_t part_and_address;
  uint32_t buffer;
  uint32_t length_and_status;
  uint32_t crc;
} FlashCommand;

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
} SaradcConfig;

typedef struct {
  uint32_t parameter;
  uint16_t samples[32];
  uint32_t size;
  uint32_t timeout;
  int16_t status;
  uint8_t sample_count;
  uint16_t calibration_data;
  uint32_t crc;
  SaradcConfig config;
  void *callback;
  void *context;
} SaradcCommand;

enum {
  SARADC_ACQUIRE = 0,
  SARADC_INIT = 1,
  SARADC_ENABLE_BYPASS_CALIBRATION = 2,
  SARADC_START = 3,
  SARADC_READ_RAW = 4,
  SARADC_READ_RAW_DONE = 5,
  SARADC_STOP = 6,
  SARADC_DEINIT = 7,
  SARADC_RELEASE = 8,
  SARADC_SET_CONFIG = 9,
  SARADC_INIT_GPIO = 29,
  SARADC_DEINIT_GPIO = 30,
};

static T5AIMbIpcSocket *s_flash_socket;
static T5AIMbIpcSocket *s_saradc_socket;
static PebbleMutex *s_flash_mutex;
static PebbleMutex *s_saradc_mutex;
static FlashCommand s_flash_command __attribute__((aligned(32)));
static SaradcCommand s_saradc_command __attribute__((aligned(32)));
static bool s_flash_available;
static bool s_saradc_available = true;

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(SaradcConfig) == 36u, "BK7258 SARADC config ABI changed");
_Static_assert(sizeof(SaradcCommand) == 132u, "BK7258 SARADC command ABI changed");
#endif

static uint32_t prv_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0u;
  while (length-- != 0u) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8u; ++bit) {
      crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
    }
  }
  return crc;
}

static uint32_t prv_saradc_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0u;
  while (length-- != 0u) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8u; ++bit) {
      crc = (crc & 0x80000000u) != 0u ? (crc << 1u) ^ 0x31u : crc << 1u;
    }
  }
  return crc;
}

static void prv_saradc_notify(uint8_t channel, const T5AICPMailboxFrame *frame, void *context) {
  (void)channel;
  (void)context;
  if (frame->length != 16u) {
    (void)t5ai_cp_mailbox_acknowledge_frame(frame, false);
    return;
  }
  uint32_t *words = (uint32_t *)(uintptr_t)frame->address;
  words[1] = SARADC_NOTIFY_ACK;
  (void)t5ai_cp_mailbox_acknowledge_frame(frame, true);
}

status_t t5ai_flash_ipc_init(void) {
  s_flash_mutex = mutex_create();
  s_saradc_mutex = mutex_create();
  if (s_flash_mutex == NULL || s_saradc_mutex == NULL) {
    return E_OUT_OF_MEMORY;
  }
  status_t status = t5ai_mb_ipc_init();
  if (FAILED(status)) {
    return status;
  }
  status = t5ai_cp_mailbox_register(SARADC_NOTIFY_RX_CHANNEL, prv_saradc_notify, NULL);
  if (FAILED(status) && status != E_INVALID_OPERATION) {
    return status;
  }
  status = t5ai_mb_ipc_open(FLASH_CLIENT_PORT, FLASH_SERVER_PORT, &s_flash_socket);
  s_flash_available = PASSED(status);
  return status;
}

static void prv_flash_fault(status_t status, uint8_t command) {
  const bool ambiguous = status == E_AGAIN;
  if (ambiguous) {
    s_flash_available = false;
    t5ai_mb_ipc_poison(s_flash_socket);
  }
  t5ai_cp_mailbox_snapshot_failure(0x464c0000u | command, status);
  PBL_LOG_ERR("T5AI flash IPC fault command=%u status=%ld quarantined=%u", command,
              (long)status, ambiguous);
}

static status_t prv_flash_transaction(uint8_t command) {
  if (!s_flash_available) {
    PBL_LOG_ERR("T5AI flash IPC unavailable for command %u", command);
    return E_INVALID_OPERATION;
  }
  t5ai_mb_ipc_discard(s_flash_socket);
  status_t status = t5ai_mb_ipc_send(s_flash_socket, command, &s_flash_command,
                                     sizeof(s_flash_command), IPC_TIMEOUT_MS);
  if (FAILED(status)) {
    prv_flash_fault(status, command);
    return status;
  }
  for (unsigned stale = 0u; stale <= IPC_STALE_RESPONSE_LIMIT; ++stale) {
    uint8_t received_command = UINT8_MAX;
    FlashCommand response;
    status = t5ai_mb_ipc_receive(s_flash_socket, &received_command, &response,
                                 sizeof(response), IPC_TIMEOUT_MS);
    if (FAILED(status)) {
      PBL_LOG_ERR("T5AI flash completion failed expected=%u status=%ld", command,
                  (long)status);
      prv_flash_fault(status, command);
      return status;
    }
    if (received_command != command) {
      PBL_LOG_WRN("T5AI flash ignored stale completion expected=%u received=%u count=%u",
                  command, received_command, stale + 1u);
      continue;
    }
    s_flash_command = response;
    const int16_t api_status = (int16_t)(response.length_and_status >> 16u);
    if (api_status != 0) {
      PBL_LOG_ERR("T5AI flash CP error command=%u api=%d length=%u buffer=%08lx", command,
                  api_status, (unsigned)(uint16_t)response.length_and_status,
                  (unsigned long)response.buffer);
      prv_flash_fault(E_ERROR, command);
      return E_ERROR;
    }
    return S_SUCCESS;
  }
  PBL_LOG_ERR("T5AI flash stale completion limit expected=%u", command);
  prv_flash_fault(E_ERROR, command);
  return E_ERROR;
}

status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length) {
  if (buffer == NULL || length == 0u || length > 0xffffu) {
    return E_INVALID_ARGUMENT;
  }
  mutex_lock(s_flash_mutex);
  s_flash_command = (FlashCommand){
      .part_and_address = offset << 8u,
      .length_and_status = (uint32_t)length,
  };
  status_t status = prv_flash_transaction(FLASH_READ);
  if (FAILED(status) ||
      !t5ai_cp_mailbox_shared_range_valid((uintptr_t)s_flash_command.buffer, length)) {
    status = FAILED(status) ? status : E_ERROR;
    if (s_flash_available) {
      prv_flash_fault(status, FLASH_READ);
    }
    goto out;
  }
  const void *source = (const void *)(uintptr_t)s_flash_command.buffer;
  dcache_invalidate((void *)source, length);
  if (prv_crc32(source, length) != s_flash_command.crc) {
    status = E_ERROR;
    prv_flash_fault(status, FLASH_READ);
    goto out;
  }
  memcpy(buffer, source, length);

  memset(&s_flash_command, 0, sizeof(s_flash_command));
  status = prv_flash_transaction(FLASH_READ_DONE);
out:
  mutex_unlock(s_flash_mutex);
  return status;
}

status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length) {
  if (buffer == NULL || length == 0u || length > 0xffffu) {
    return E_INVALID_ARGUMENT;
  }
  mutex_lock(s_flash_mutex);
  s_flash_command = (FlashCommand){
      .part_and_address = offset << 8u,
      .buffer = (uint32_t)(uintptr_t)buffer,
      .length_and_status = (uint32_t)length,
      .crc = prv_crc32(buffer, length),
  };
  dcache_flush((void *)buffer, length);
  const status_t status = prv_flash_transaction(FLASH_WRITE);
  mutex_unlock(s_flash_mutex);
  return status;
}

status_t t5ai_flash_ipc_erase_4k(uint32_t offset) {
  if ((offset & 0xfffu) != 0u) {
    return E_INVALID_ARGUMENT;
  }
  mutex_lock(s_flash_mutex);
  s_flash_command = (FlashCommand){.part_and_address = offset << 8u};
  const status_t status = prv_flash_transaction(FLASH_ERASE_SECTOR);
  mutex_unlock(s_flash_mutex);
  return status;
}

static status_t prv_saradc_connect(void) {
  if (!s_saradc_available) {
    return E_INVALID_OPERATION;
  }
  if (s_saradc_socket != NULL) {
    return S_SUCCESS;
  }
  return t5ai_mb_ipc_open(SARADC_CLIENT_PORT, SARADC_SERVER_PORT, &s_saradc_socket);
}

static void prv_saradc_disconnect(void) {
  t5ai_mb_ipc_poison(s_saradc_socket);
  s_saradc_available = false;
}

static status_t prv_saradc_transaction(uint8_t command) {
  t5ai_mb_ipc_discard(s_saradc_socket);
  status_t status = t5ai_mb_ipc_send(s_saradc_socket, command, &s_saradc_command,
                                     sizeof(s_saradc_command), SARADC_IPC_TIMEOUT_MS);
  if (FAILED(status)) {
    if (status == E_AGAIN) {
      prv_saradc_disconnect();
    }
    return status;
  }
  for (unsigned stale = 0u; stale <= IPC_STALE_RESPONSE_LIMIT; ++stale) {
    uint8_t received_command = UINT8_MAX;
    SaradcCommand response;
    status = t5ai_mb_ipc_receive(s_saradc_socket, &received_command, &response,
                                 sizeof(response), SARADC_IPC_TIMEOUT_MS);
    if (FAILED(status)) {
      if (status == E_AGAIN) {
        prv_saradc_disconnect();
      }
      return status;
    }
    if (received_command != command) {
      PBL_LOG_WRN("T5AI SARADC ignored stale completion expected=%u received=%u count=%u",
                  command, received_command, stale + 1u);
      continue;
    }
    s_saradc_command = response;
    return response.status == 0 ? S_SUCCESS : E_ERROR;
  }
  return E_ERROR;
}

static status_t prv_saradc_cleanup(uint8_t channel, status_t result) {
  if (!s_saradc_available) {
    return result;
  }
  status_t cleanup_status = S_SUCCESS;
  memset(&s_saradc_command, 0, sizeof(s_saradc_command));
  status_t status = prv_saradc_transaction(SARADC_STOP);
  if (FAILED(status)) {
    return status;
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_DEINIT);
  if (FAILED(status)) {
    return status;
  }
  status = prv_saradc_transaction(SARADC_RELEASE);
  if (FAILED(status)) {
    return status;
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_DEINIT_GPIO);
  cleanup_status = FAILED(status) ? status : cleanup_status;
  return FAILED(result) ? result : cleanup_status;
}

static status_t prv_saradc_read_peak_locked(uint8_t channel, uint8_t samples, uint16_t *peak) {
  status_t status = prv_saradc_connect();
  if (FAILED(status)) {
    return status;
  }
  memset(&s_saradc_command, 0, sizeof(s_saradc_command));
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_INIT_GPIO);
  if (FAILED(status)) {
    return status;
  }
  status = prv_saradc_transaction(SARADC_ACQUIRE);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  s_saradc_command.config.channel = channel;
  status = prv_saradc_transaction(SARADC_INIT);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }

  memset(&s_saradc_command.config, 0, sizeof(s_saradc_command.config));
  s_saradc_command.config.clock = 0x30a0c5u;
  s_saradc_command.config.mode = 3u;
  s_saradc_command.config.source_clock = 1u;
  s_saradc_command.config.channel = channel;
  s_saradc_command.config.saturate_mode = 4u;
  s_saradc_command.config.steady = 7u;
  s_saradc_command.config.output_buffer = s_saradc_command.samples;
  s_saradc_command.config.output_length = samples;
  status = prv_saradc_transaction(SARADC_SET_CONFIG);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  status = prv_saradc_transaction(SARADC_ENABLE_BYPASS_CALIBRATION);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }
  status = prv_saradc_transaction(SARADC_START);
  if (FAILED(status)) {
    return prv_saradc_cleanup(channel, status);
  }

  memset(s_saradc_command.samples, 0, sizeof(s_saradc_command.samples));
  s_saradc_command.size = samples;
  s_saradc_command.timeout = SARADC_IPC_TIMEOUT_MS;
  status = prv_saradc_transaction(SARADC_READ_RAW);
  if (PASSED(status) && s_saradc_command.size == samples &&
      prv_saradc_crc32(s_saradc_command.samples, samples * sizeof(uint16_t)) ==
          s_saradc_command.crc) {
    uint16_t maximum = 0u;
    for (uint8_t i = 0; i < samples; ++i) {
      if (s_saradc_command.samples[i] > maximum) {
        maximum = s_saradc_command.samples[i];
      }
    }
    *peak = maximum;
    status = prv_saradc_transaction(SARADC_READ_RAW_DONE);
  } else {
    status = E_ERROR;
  }
  return prv_saradc_cleanup(channel, status);
}

status_t t5ai_saradc_read_peak(uint8_t channel, uint8_t samples, uint16_t *peak) {
  if (peak == NULL || channel > 15u || samples == 0u || samples > 32u) {
    return E_INVALID_ARGUMENT;
  }
  mutex_lock(s_saradc_mutex);
  const status_t status = prv_saradc_read_peak_locked(channel, samples, peak);
  mutex_unlock(s_saradc_mutex);
  return status;
}
