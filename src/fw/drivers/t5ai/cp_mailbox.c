/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/mcu/cache.h>
#include <pbl/mcu/interrupts.h>
#include <pbl/os/mutex.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mailbox_channel.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CP_CONTROL_CHANNEL 0x10u
#define CP_POWER_UP_COMMAND 1u
#define CP_HEARTBEAT_COMMAND 2u
#define CP_PM_TX_CHANNEL 0x12u
#define CP_PM_RX_CHANNEL 0x42u
#define CP_PM_CLOCK_COMMAND 2u
#define CP_PM_UART0_CLOCK 2u
#define CP_PM_CLOCK_ENABLE 1u
#define CP_PM_POWER_COMMAND 1u
#define CP_PM_SLEEP_COMMAND 3u
#define CP_PM_PSRAM_POWER_COMMAND 7u
#define CP_PM_CPU_FREQUENCY_COMMAND 4u
#define CP_PM_QSPI0_CLOCK 20u
#define CP_PM_AHBP_QSPI_POWER 101u
#define CP_PM_POWER_ON 0u
#define CP_PM_PSRAM_APP_MODULE 9u
#define CP_PM_SLEEP_APP_MODULE 12u
#define CP_PM_CPU1_MODULE 38u
#define CP_LOG_CHANNEL 0x1eu
#define CP_LOG_ASSERT_OUT_COMMAND 4u
#define CP_LOG_STATIC_TAG 0xffffu
#define CP_LOG_BUFFER_SIZE 160u
#define CP_AON_RTC_LOW (*(volatile uint32_t *)0x4400020cu)
#define CP_AON_RTC_HZ 32000u
#define CP_FLASH_NOTIFY_RX_CHANNEL 0x4bu
#define CP_FLASH_NOTIFY_REQUEST 1u
#define CP_FLASH_NOTIFY_ACK 2u
#define CP_RUNTIME_TIMEOUT_MS 2000u
#define CP_PM_TIMEOUT_MS 100u
#define CP_STARTUP_TIMEOUT_MS 2000u
#define CP_QUEUE_DEPTH 4u

typedef struct {
  T5AICPMailboxFrame frames[CP_QUEUE_DEPTH];
  uint8_t head;
  uint8_t count;
  SemaphoreHandle_t ready;
} ReceiveQueue;

typedef struct {
  T5AICPMailboxReceiveCallback callback;
  void *context;
} Route;

static Route s_routes[T5AI_MAILBOX_CHANNEL_COUNT];
/* Only the PM protocol has a blocking compatibility consumer. Routed clients
 * (MIPC, HCI, flash notifications, and logging) own their receive state and
 * must not allocate dormant per-channel queues. */
static ReceiveQueue s_pm_queue;
static PebbleMutex *s_control_mutex;
static T5AIMailboxMessage s_control_message __attribute__((aligned(32)));
static T5AIMailboxMessage s_pm_message __attribute__((aligned(32)));
static T5AIMailboxMessage s_log_message __attribute__((aligned(32)));
static uint8_t s_log_buffer[CP_LOG_BUFFER_SIZE] __attribute__((aligned(32)));
static uint32_t s_heartbeat_parameter __attribute__((aligned(32)));
static uint8_t s_control_sequence;
static bool s_runtime_initialized;
static T5AICPMailboxDiagnostics s_diagnostics;
static T5AICPMailboxFailureDiagnostics s_failure;
static bool s_failure_valid;

static bool prv_enqueue(uint8_t channel, const T5AIMailboxMessage *message) {
  if (channel != CP_PM_RX_CHANNEL) {
    return false;
  }
  ReceiveQueue *queue = &s_pm_queue;
  taskENTER_CRITICAL();
  if (queue->count == CP_QUEUE_DEPTH) {
    taskEXIT_CRITICAL();
    return false;
  }
  const uint8_t tail = (uint8_t)((queue->head + queue->count) % CP_QUEUE_DEPTH);
  T5AICPMailboxFrame *frame = &queue->frames[tail];
  memcpy(frame->snapshot, message, sizeof(*message));
  frame->address = (uintptr_t)frame->snapshot;
  frame->length = sizeof(*message);
  ++queue->count;
  taskEXIT_CRITICAL();
  (void)xSemaphoreGive(queue->ready);
  return true;
}

static bool prv_dequeue(uint8_t channel, T5AICPMailboxFrame *frame) {
  if (channel != CP_PM_RX_CHANNEL) {
    return false;
  }
  ReceiveQueue *queue = &s_pm_queue;
  taskENTER_CRITICAL();
  if (queue->count == 0u) {
    taskEXIT_CRITICAL();
    return false;
  }
  *frame = queue->frames[queue->head];
  memcpy(frame->snapshot, (const void *)frame->address, frame->length);
  frame->address = (uintptr_t)frame->snapshot;
  queue->head = (uint8_t)((queue->head + 1u) % CP_QUEUE_DEPTH);
  --queue->count;
  taskEXIT_CRITICAL();
  return true;
}

static void prv_receive(T5AIMailboxMessage *message, void *context) {
  const uint8_t channel = (uint8_t)(uintptr_t)context;
  const uint8_t index = channel & 0x0fu;
  if (channel == CP_FLASH_NOTIFY_RX_CHANNEL) {
    if (message->parameter1 == CP_FLASH_NOTIFY_REQUEST) {
      message->parameter1 = CP_FLASH_NOTIFY_ACK;
      return;
    }
    message->header |= 1u << 8u;
    return;
  }

  Route route = s_routes[index];
  if (route.callback != NULL) {
    T5AICPMailboxFrame frame = {
        .address = (uintptr_t)message,
        .length = sizeof(*message),
    };
    memcpy(frame.snapshot, message, sizeof(*message));
    frame.address = (uintptr_t)frame.snapshot;
    route.callback(channel, &frame, route.context);
    memcpy(message, frame.snapshot, sizeof(*message));
    return;
  }
  if (channel == CP_PM_RX_CHANNEL && prv_enqueue(channel, message)) {
    return;
  }
  message->header |= 1u << 8u;
}

static void prv_transmit_complete(const T5AIMailboxMessage *ack, status_t status, void *context) {
  (void)ack;
  (void)context;
  if (PASSED(status)) {
    ++s_diagnostics.tx_retired;
  } else {
    ++s_diagnostics.send_failures;
  }
}

static status_t prv_ensure_runtime(void) {
  if (s_runtime_initialized) {
    return S_SUCCESS;
  }
  s_pm_queue.ready = xSemaphoreCreateCounting(CP_QUEUE_DEPTH, 0u);
  if (s_pm_queue.ready == NULL) {
    return E_OUT_OF_MEMORY;
  }
  s_control_mutex = mutex_create();
  if (s_control_mutex == NULL) {
    return E_OUT_OF_MEMORY;
  }
  status_t status = t5ai_mailbox_channel_init();
  if (FAILED(status)) {
    return status;
  }
  for (uint8_t i = 0; i < T5AI_MAILBOX_CHANNEL_COUNT; ++i) {
    const uint8_t channel = T5AI_MAILBOX_RX_CHANNEL_BASE + i;
    status = t5ai_mailbox_channel_register(channel, prv_receive, (void *)(uintptr_t)channel);
    if (FAILED(status)) {
      return status;
    }
  }
  s_runtime_initialized = true;
  return S_SUCCESS;
}

status_t t5ai_cp_mailbox_lock(void) {
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  mutex_lock(s_control_mutex);
  return S_SUCCESS;
}

void t5ai_cp_mailbox_unlock(void) {
  if (s_control_mutex != NULL) {
    mutex_unlock(s_control_mutex);
  }
}

status_t t5ai_cp_mailbox_send(const void *data, size_t length) {
  if (data == NULL || length != sizeof(T5AIMailboxMessage)) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  T5AIMailboxMessage message;
  memcpy(&message, data, sizeof(message));
  const uint8_t channel = (uint8_t)(message.header >> 24u);
  ++s_diagnostics.tx_queued;
  status = t5ai_mailbox_channel_send_async(channel, &message, prv_transmit_complete,
                                            (void *)(uintptr_t)channel);
  if (PASSED(status)) {
    ++s_diagnostics.tx_published;
  } else {
    ++s_diagnostics.send_failures;
  }
  return status;
}

status_t t5ai_cp_mailbox_send_priority(const void *data, size_t length) {
  return t5ai_cp_mailbox_send(data, length);
}

status_t t5ai_cp_mailbox_register(uint8_t logical_channel, T5AICPMailboxReceiveCallback callback,
                                  void *context) {
  if ((logical_channel & 0xf0u) != T5AI_MAILBOX_RX_CHANNEL_BASE || callback == NULL) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  const uint8_t index = logical_channel & 0x0fu;
  taskENTER_CRITICAL();
  if (s_routes[index].callback != NULL) {
    taskEXIT_CRITICAL();
    return E_INVALID_OPERATION;
  }
  s_routes[index] = (Route){.callback = callback, .context = context};
  taskEXIT_CRITICAL();
  return S_SUCCESS;
}

void t5ai_cp_mailbox_unregister(uint8_t logical_channel) {
  if ((logical_channel & 0xf0u) != T5AI_MAILBOX_RX_CHANNEL_BASE) {
    return;
  }
  taskENTER_CRITICAL();
  s_routes[logical_channel & 0x0fu] = (Route){0};
  taskEXIT_CRITICAL();
}

status_t t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *frame, bool handled) {
  if (frame == NULL || frame->address == 0u || frame->length != sizeof(T5AIMailboxMessage)) {
    return E_INVALID_ARGUMENT;
  }
  T5AIMailboxMessage *message = (T5AIMailboxMessage *)(uintptr_t)frame->address;
  if (!handled) {
    message->header |= 1u << 8u;
  }
  return S_SUCCESS;
}

status_t t5ai_cp_mailbox_process_pending(void) {
  return prv_ensure_runtime();
}

void t5ai_cp_mailbox_enable_rx_interrupt(void) {
  (void)prv_ensure_runtime();
}

status_t t5ai_cp_mailbox_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame) {
  if (frame == NULL) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  if (logical_channel != CP_PM_RX_CHANNEL) {
    return E_INVALID_ARGUMENT;
  }
  if (xSemaphoreTake(s_pm_queue.ready, pdMS_TO_TICKS(CP_RUNTIME_TIMEOUT_MS)) != pdPASS) {
    ++s_diagnostics.receive_timeouts;
    return E_AGAIN;
  }
  return prv_dequeue(logical_channel, frame) ? S_SUCCESS : E_AGAIN;
}

status_t t5ai_cp_mailbox_try_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame) {
  if (frame == NULL) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  if (logical_channel != CP_PM_RX_CHANNEL ||
      xSemaphoreTake(s_pm_queue.ready, 0u) != pdPASS) {
    return E_AGAIN;
  }
  return prv_dequeue(logical_channel, frame) ? S_SUCCESS : E_ERROR;
}

void t5ai_cp_mailbox_discard_pending(void) {
  if (!s_runtime_initialized) {
    return;
  }
  taskENTER_CRITICAL();
  s_pm_queue.head = 0u;
  s_pm_queue.count = 0u;
  taskEXIT_CRITICAL();
  while (xSemaphoreTake(s_pm_queue.ready, 0u) == pdPASS) {
  }
}

void t5ai_cp_mailbox_announce_power_up(void) {
  s_control_message = (T5AIMailboxMessage){.header = CP_POWER_UP_COMMAND};
  T5AIMailboxMessage ack;
  (void)t5ai_mailbox_channel_bootstrap_send(CP_CONTROL_CHANNEL, &s_control_message, &ack,
                                            CP_STARTUP_TIMEOUT_MS);
}

void t5ai_cp_mailbox_heartbeat(void) {
  if (!s_runtime_initialized) {
    return;
  }
  s_heartbeat_parameter = 0u;
  ++s_control_sequence;
  s_control_message = (T5AIMailboxMessage){
      .header = CP_HEARTBEAT_COMMAND | ((uint32_t)s_control_sequence << 16u) |
                ((uint32_t)CP_CONTROL_CHANNEL << 24u),
      .parameter1 = (uint32_t)(uintptr_t)&s_heartbeat_parameter,
      .parameter2 = sizeof(s_heartbeat_parameter),
  };
  dcache_flush(&s_heartbeat_parameter, sizeof(s_heartbeat_parameter));
  (void)t5ai_cp_mailbox_send(&s_control_message, sizeof(s_control_message));
}

static status_t prv_pm_request(uint8_t command, uint32_t parameter1, uint32_t parameter2) {
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  s_pm_message = (T5AIMailboxMessage){
      .header = command,
      .parameter1 = parameter1,
      .parameter2 = parameter2,
  };
  T5AIMailboxMessage physical_ack;
  status = t5ai_mailbox_channel_send(CP_PM_TX_CHANNEL, &s_pm_message, &physical_ack,
                                     CP_RUNTIME_TIMEOUT_MS);
  if (FAILED(status)) {
    return status;
  }
  T5AICPMailboxFrame frame;
  if (xSemaphoreTake(s_pm_queue.ready, pdMS_TO_TICKS(CP_PM_TIMEOUT_MS)) != pdPASS ||
      !prv_dequeue(CP_PM_RX_CHANNEL, &frame)) {
    return E_AGAIN;
  }
  const T5AIMailboxMessage *response = (const void *)frame.address;
  return (uint8_t)response->header == command && response->parameter1 == 0u ? S_SUCCESS : E_ERROR;
}

status_t t5ai_cp_mailbox_enable_uart0_clock(void) {
  return prv_pm_request(CP_PM_CLOCK_COMMAND, CP_PM_UART0_CLOCK, CP_PM_CLOCK_ENABLE);
}

status_t t5ai_cp_mailbox_enable_qspi0(void) {
  status_t status =
      prv_pm_request(CP_PM_POWER_COMMAND, CP_PM_AHBP_QSPI_POWER, CP_PM_POWER_ON);
  return FAILED(status)
             ? status
             : prv_pm_request(CP_PM_CLOCK_COMMAND, CP_PM_QSPI0_CLOCK, CP_PM_CLOCK_ENABLE);
}

status_t t5ai_cp_mailbox_enable_psram(void) {
  return prv_pm_request(CP_PM_PSRAM_POWER_COMMAND, CP_PM_PSRAM_APP_MODULE, CP_PM_POWER_ON);
}

status_t t5ai_cp_mailbox_vote_app_sleep(bool allow_sleep) {
  return prv_pm_request(CP_PM_SLEEP_COMMAND, CP_PM_SLEEP_APP_MODULE, allow_sleep ? 1u : 0u);
}

status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency frequency) {
  if (frequency > T5AICpuFrequency_480MHz) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = t5ai_cp_mailbox_lock();
  if (FAILED(status)) {
    return status;
  }
  status = prv_pm_request(CP_PM_CPU_FREQUENCY_COMMAND, CP_PM_CPU1_MODULE, frequency);
  t5ai_cp_mailbox_unlock();
  return status;
}

static status_t prv_wait_for_log_buffer_release(bool scheduler_running) {
  const uint32_t start = CP_AON_RTC_LOW;
  const uint32_t timeout_ticks = CP_RUNTIME_TIMEOUT_MS * (CP_AON_RTC_HZ / 1000u);
  do {
    dcache_invalidate(s_log_buffer, 1u);
    if (s_log_buffer[0] == 0u) {
      return S_SUCCESS;
    }
    if (scheduler_running) {
      vTaskDelay(pdMS_TO_TICKS(1u));
    }
  } while ((uint32_t)(CP_AON_RTC_LOW - start) < timeout_ticks);
  return E_AGAIN;
}

status_t t5ai_cp_mailbox_log(const void *data, size_t length) {
  if (data == NULL || length == 0u) {
    return E_INVALID_ARGUMENT;
  }
  /* Runtime logging is synchronous and owns shared staging storage.  Callers
   * in an interrupt must use the dbgserial front end, which intentionally
   * drops T5AI interrupt-time output rather than blocking on the CP. */
  if (mcu_state_is_isr()) {
    return E_INVALID_OPERATION;
  }
  const bool scheduler_running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
  if (scheduler_running) {
    status_t status = prv_ensure_runtime();
    if (FAILED(status)) {
      return status;
    }
  }
  const uint8_t *cursor = data;
  while (length != 0u) {
    const size_t chunk = length < sizeof(s_log_buffer) - 1u ? length : sizeof(s_log_buffer) - 1u;
    s_log_buffer[0] = 1u;
    memcpy(&s_log_buffer[1], cursor, chunk);
    s_log_message = (T5AIMailboxMessage){
        .header = CP_LOG_ASSERT_OUT_COMMAND,
        .parameter1 = (uint32_t)(uintptr_t)s_log_buffer,
        .parameter2 = (uint32_t)(chunk + 1u) | (CP_LOG_STATIC_TAG << 16u),
    };
    dcache_flush(s_log_buffer, chunk + 1u);
    T5AIMailboxMessage ack;
    status_t status =
        scheduler_running
            ? t5ai_mailbox_channel_send(CP_LOG_CHANNEL, &s_log_message, &ack,
                                        CP_RUNTIME_TIMEOUT_MS)
            : t5ai_mailbox_channel_bootstrap_send(CP_LOG_CHANNEL, &s_log_message, &ack,
                                                  CP_RUNTIME_TIMEOUT_MS);
    if (PASSED(status)) {
      status = prv_wait_for_log_buffer_release(scheduler_running);
    }
    if (FAILED(status)) {
      return status;
    }
    cursor += chunk;
    length -= chunk;
  }
  return S_SUCCESS;
}

void t5ai_cp_mailbox_get_diagnostics(T5AICPMailboxDiagnostics *diagnostics) {
  if (diagnostics != NULL) {
    *diagnostics = s_diagnostics;
  }
}

void t5ai_cp_mailbox_snapshot_failure(uint32_t context, status_t status) {
  memset(&s_failure, 0, sizeof(s_failure));
  s_failure.context = context;
  s_failure.status = status;
  s_failure.tx_queued = s_diagnostics.tx_queued;
  s_failure.tx_published = s_diagnostics.tx_published;
  s_failure.tx_retired = s_diagnostics.tx_retired;
  s_failure.send_failures = s_diagnostics.send_failures;
  s_failure.receive_timeouts = s_diagnostics.receive_timeouts;
  s_failure_valid = true;
}

bool t5ai_cp_mailbox_get_failure_diagnostics(T5AICPMailboxFailureDiagnostics *diagnostics) {
  if (!s_failure_valid || diagnostics == NULL) {
    return false;
  }
  *diagnostics = s_failure;
  return true;
}

void t5ai_cp_mailbox_clear_failure_diagnostics(void) {
  s_failure_valid = false;
}
