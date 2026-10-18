/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/time.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/saradc_ipc.h>
#define SARADC_MAX_CHANNEL       15u
#define SARADC_MAX_SAMPLES       32u
#define SARADC_CONFIG_WIRE_SIZE  36u
#define SARADC_COMMAND_WIRE_SIZE 132u
#define SARADC_NOTIFY_WORDS      4u
#define SARADC_NOTIFY_ACK_WORD   1u
// Preserve the raw vendor clock configuration and continuous sampling setup.
#define SARADC_CLOCK_CONFIG    0x30a0c5u
#define SARADC_MODE_CONTINUOUS 3u
#define SARADC_SOURCE_XTAL     1u
#define SARADC_SATURATE_MODE   4u
#define SARADC_STEADY_COUNT    7u
// Vendor tests bit 7 but retains the full 32-bit accumulator (no byte masking).
#define SARADC_CRC_HIGH_BIT      0x80u
#define SARADC_CRC_POLYNOMIAL    0x31u
#define SARADC_CRC_BITS_PER_BYTE 8u

#define SARADC_CLIENT_PORT       17u
#define SARADC_SERVER_PORT       2u
#define SARADC_IPC_TIMEOUT_MS    2000u
#define SARADC_NOTIFY_RX_CHANNEL 0x4cu
#define SARADC_NOTIFY_ACK        2u
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
  uint16_t samples[SARADC_MAX_SAMPLES];
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

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(SaradcConfig) == SARADC_CONFIG_WIRE_SIZE, "SARADC config ABI changed");
_Static_assert(sizeof(SaradcCommand) == SARADC_COMMAND_WIRE_SIZE, "SARADC command ABI changed");
#endif
static uint32_t prv_saradc_crc32(const void *data, size_t length) {
  const uint8_t *bytes = data;
  uint32_t crc = 0u;
  while (length-- != 0u) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < SARADC_CRC_BITS_PER_BYTE; ++bit) {
      crc = (crc & SARADC_CRC_HIGH_BIT) != 0u ? (crc << 1u) ^ SARADC_CRC_POLYNOMIAL : crc << 1u;
    }
  }
  return crc;
}

static void prv_saradc_notify(uint8_t channel, const T5AICPMailboxFrame *frame, void *context) {
  (void)channel;
  (void)context;
  if (frame->length != SARADC_NOTIFY_WORDS * sizeof(uint32_t)) {
    (void)t5ai_cp_mailbox_acknowledge_frame(frame, false);
    return;
  }
  uint32_t *words = (uint32_t *)(uintptr_t)frame->address;
  words[SARADC_NOTIFY_ACK_WORD] = SARADC_NOTIFY_ACK;
  (void)t5ai_cp_mailbox_acknowledge_frame(frame, true);
}

#define SARADC_SESSION_TIMEOUT_MS 5000u
#define SARADC_CONNECT_TIMEOUT_MS 500u

static T5AIMbIpcSocket *s_socket;
static SaradcCommand s_command __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static SaradcCommand s_response;
static uint16_t s_adc_buffer[SARADC_MAX_SAMPLES] __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static bool s_initialized, s_connecting, s_have_error;
static T5AISaradcDiagnostics s_diagnostics;
static bool s_active;
static bool s_available = true;
static bool s_gpio, s_acquired, s_adc_initialized, s_started, s_cleanup_failed;
static uint8_t s_channel, s_count, s_step;
static uint16_t s_peak;
static uint64_t s_deadline, s_began;
static status_t s_result;
static T5AISaradcCompletion s_callback;
static void *s_context;

static const uint8_t s_steps[] = {
  SARADC_INIT_GPIO,
  SARADC_ACQUIRE,
  SARADC_INIT,
  SARADC_SET_CONFIG,
  SARADC_ENABLE_BYPASS_CALIBRATION,
  SARADC_START,
  SARADC_READ_RAW,
  SARADC_READ_RAW_DONE,
  SARADC_STOP,
  SARADC_DEINIT,
  SARADC_RELEASE,
  SARADC_DEINIT_GPIO,
};
#define SARADC_CLEANUP_STEP 8u

static void prv_record_error(uint8_t command, int16_t cp_status, uint32_t invalid_count) {
  if (!s_have_error) {
    s_diagnostics.failed_command = command;
    s_diagnostics.cp_status = cp_status;
    s_diagnostics.invalid_count = invalid_count;
    s_have_error = true;
  }
}

void t5ai_saradc_get_diagnostics(T5AISaradcDiagnostics *diagnostics) {
  pbl_irq_lock();
  *diagnostics = s_diagnostics;
  pbl_irq_unlock();
}

static void prv_finish(status_t status) {
  T5AISaradcCompletion callback = s_callback;
  void *context = s_context;
  if (FAILED(status)) {
    prv_record_error(s_connecting ? UINT8_MAX : s_steps[s_step], 0, 0);
  }
  s_diagnostics.status = status;
  s_diagnostics.elapsed_ms = pbl_uptime_ms() - s_began;
  s_diagnostics.available = s_available;
  pbl_irq_lock();
  s_active = false;
  pbl_irq_unlock();
  callback(status, PASSED(status) ? s_peak : 0u, context);
}

static void prv_issue(void);

static void prv_completed(status_t status, void *context) {
  (void)context;
  if (FAILED(status)) {
    // A transport failure is ambiguous. Retain every CP-visible object until reset.
    s_available = false;
    prv_finish(status);
    return;
  }
  const uint8_t command = s_steps[s_step];
  const bool success = s_response.status == 0;
  if (!success) {
    prv_record_error(command, s_response.status, 0);
    if (PASSED(s_result)) {
      s_result = E_ERROR;
    }
    if (s_step >= SARADC_CLEANUP_STEP) {
      s_cleanup_failed = true;
    }
  }
  if (success) {
    switch (command) {
      case SARADC_INIT_GPIO:
        s_gpio = true;
        break;
      case SARADC_ACQUIRE:
        s_acquired = true;
        break;
      case SARADC_INIT:
        s_adc_initialized = true;
        break;
      case SARADC_START:
        s_started = true;
        break;
      case SARADC_STOP:
        s_started = false;
        break;
      case SARADC_DEINIT:
        s_adc_initialized = false;
        break;
      case SARADC_RELEASE:
        s_acquired = false;
        break;
      case SARADC_DEINIT_GPIO:
        s_gpio = false;
        break;
      case SARADC_READ_RAW:
        if (s_response.size != s_count ||
            prv_saradc_crc32(s_response.samples, s_count * sizeof(uint16_t)) != s_response.crc) {
          prv_record_error(command, 0, s_response.size);
          s_result = E_ERROR;
        } else {
          for (uint8_t i = 0; i < s_count; ++i) {
            if (s_response.samples[i] > s_peak) {
              s_peak = s_response.samples[i];
            }
          }
        }
        // Even rejected data must complete CP's READ_RAW_DONE phase before STOP.
        ++s_step;
        prv_issue();
        return;
      default:
        break;
    }
  }
  if (!success && s_step < SARADC_CLEANUP_STEP) {
    s_step = SARADC_CLEANUP_STEP;
  } else {
    ++s_step;
  }
  prv_issue();
}

static void prv_issue(void) {
  while (s_step < sizeof(s_steps)) {
    const uint8_t command = s_steps[s_step];
    if ((command == SARADC_STOP && !s_started) ||
        (command == SARADC_DEINIT && !s_adc_initialized) ||
        (command == SARADC_RELEASE && !s_acquired) || (command == SARADC_DEINIT_GPIO && !s_gpio)) {
      ++s_step;
      continue;
    }
    if (pbl_uptime_ms() >= s_deadline) {
      t5ai_mb_ipc_poison(s_socket);
      s_available = false;
      prv_finish(E_AGAIN);
      return;
    }
    memset(&s_command, 0, sizeof(s_command));
    s_command.config.channel = s_channel;
    if (command == SARADC_SET_CONFIG) {
      s_command.config.clock = SARADC_CLOCK_CONFIG;
      s_command.config.mode = SARADC_MODE_CONTINUOUS;
      s_command.config.source_clock = SARADC_SOURCE_XTAL;
      s_command.config.saturate_mode = SARADC_SATURATE_MODE;
      s_command.config.steady = SARADC_STEADY_COUNT;
      s_command.config.output_buffer = s_adc_buffer;
      s_command.config.output_length = s_count;
    } else if (command == SARADC_READ_RAW) {
      s_command.size = s_count;
      s_command.timeout = SARADC_IPC_TIMEOUT_MS;
    }
    const status_t status =
        t5ai_mb_ipc_exchange_async(s_socket, command, &s_command, &s_response, sizeof(s_command),
                                   s_deadline, prv_completed, NULL);
    if (FAILED(status)) {
      s_available = false;
      t5ai_mb_ipc_poison(s_socket);
      prv_finish(status);
    }
    return;
  }
  if (s_cleanup_failed) {
    s_available = false;
    t5ai_mb_ipc_poison(s_socket);
  }
  // Keep the final command index available to the bounded diagnostic.
  s_step = sizeof(s_steps) - 1u;
  prv_finish(s_result);
}

static void prv_connected(status_t status, void *context) {
  (void)context;
  if (FAILED(status)) {
    s_available = false;
    prv_finish(status);
    return;
  }
  s_connecting = false;
  prv_issue();
}

status_t t5ai_saradc_init(void) {
  if (s_initialized) {
    return S_SUCCESS;
  }
  status_t status = t5ai_mb_ipc_init();
  if (FAILED(status)) {
    return status;
  }
  status = t5ai_cp_mailbox_register(SARADC_NOTIFY_RX_CHANNEL, prv_saradc_notify, NULL);
  if (PASSED(status)) {
    s_initialized = true;
  }
  return status;
}

status_t t5ai_saradc_request(uint8_t channel, uint8_t samples, T5AISaradcCompletion callback,
                             void *context) {
  if (callback == NULL || channel > SARADC_MAX_CHANNEL || samples == 0u ||
      samples > SARADC_MAX_SAMPLES) {
    return E_INVALID_ARGUMENT;
  }
  pbl_irq_lock();
  if (s_active || !s_available) {
    status_t status = s_active ? E_BUSY : E_INVALID_OPERATION;
    pbl_irq_unlock();
    return status;
  }
  s_active = true;
  pbl_irq_unlock();
  s_connecting = s_socket == NULL;
  s_began = pbl_uptime_ms();
  s_deadline = s_began + SARADC_SESSION_TIMEOUT_MS;
  s_channel = channel;
  s_count = samples;
  s_step = 0u;
  s_peak = 0u;
  s_result = S_SUCCESS;
  s_have_error = false;
  s_diagnostics = (T5AISaradcDiagnostics){.available = s_available};
  s_cleanup_failed = false;
  s_callback = callback;
  s_context = context;
  status_t status = t5ai_saradc_init();
  if (PASSED(status)) {
    if (s_socket == NULL) {
      status = t5ai_mb_ipc_open_async(SARADC_CLIENT_PORT, SARADC_SERVER_PORT, &s_socket,
                                      s_began + SARADC_CONNECT_TIMEOUT_MS, prv_connected, NULL);
    } else {
      prv_issue();
    }
  }
  if (FAILED(status)) {
    pbl_irq_lock();
    s_active = false;
    pbl_irq_unlock();
  }
  return status;
}
