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
#define CP_RUNTIME_TIMEOUT_MS 5000u
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
static PebbleMutex *s_pm_mutex;
static PebbleMutex *s_log_mutex;
static T5AIMailboxMessage s_control_message __attribute__((aligned(32)));
static T5AIMailboxMessage s_pm_message __attribute__((aligned(32)));
static T5AIMailboxMessage s_log_message __attribute__((aligned(32)));
static uint8_t s_log_buffer[CP_LOG_BUFFER_SIZE] __attribute__((aligned(32)));
static uint32_t s_heartbeat_parameter __attribute__((aligned(32)));
static uint8_t s_control_sequence;
static bool s_heartbeat_in_flight;
static bool s_runtime_initialized;
static bool s_pm_faulted;
static bool s_log_faulted;
static T5AICPMailboxDiagnostics s_diagnostics;
static T5AICPMailboxFailureDiagnostics s_failure;
static bool s_failure_valid;
static uint8_t s_trace_next;

#define T5AI_SHARED_SRAM_BEGIN 0x28000000u
#define T5AI_SHARED_SRAM_END 0x29000000u
#define T5AI_SHARED_PSRAM_BEGIN 0x60000000u
#define T5AI_SHARED_PSRAM_END 0x61000000u

bool t5ai_cp_mailbox_shared_range_valid(uintptr_t address, size_t length) {
  if (length == 0u || address > UINTPTR_MAX - length) {
    return false;
  }
  const uintptr_t end = address + length;
  return (address >= T5AI_SHARED_SRAM_BEGIN && end <= T5AI_SHARED_SRAM_END) ||
         (address >= T5AI_SHARED_PSRAM_BEGIN && end <= T5AI_SHARED_PSRAM_END);
}

static void prv_trace(T5AICPMailboxTraceEvent event, uint32_t header, uint32_t parameter,
                      status_t status) {
  taskENTER_CRITICAL();
  s_diagnostics.trace[s_trace_next] = (T5AICPMailboxTraceEntry){
      .ticks = CP_AON_RTC_LOW,
      .header = header,
      .parameter = parameter,
      .status = status,
      .event = event,
      .tx_depth = s_diagnostics.tx_depth,
  };
  s_trace_next = (uint8_t)((s_trace_next + 1u) % T5AI_CP_MAILBOX_TRACE_CAPACITY);
  if (s_diagnostics.trace_count < T5AI_CP_MAILBOX_TRACE_CAPACITY) {
    ++s_diagnostics.trace_count;
  }
  taskEXIT_CRITICAL();
}

static void prv_tx_published(uint32_t header, uint32_t parameter) {
  taskENTER_CRITICAL();
  if (s_diagnostics.tx_depth != UINT8_MAX) {
    ++s_diagnostics.tx_depth;
  }
  if (s_diagnostics.tx_depth > s_diagnostics.tx_high_water) {
    s_diagnostics.tx_high_water = s_diagnostics.tx_depth;
  }
  taskEXIT_CRITICAL();
  prv_trace(T5AICPMailboxTraceEvent_TxPublished, header, parameter, S_SUCCESS);
}

static void prv_tx_completed(uint32_t header, status_t status) {
  taskENTER_CRITICAL();
  if (s_diagnostics.tx_depth != 0u) {
    --s_diagnostics.tx_depth;
  }
  taskEXIT_CRITICAL();
  prv_trace(PASSED(status) ? T5AICPMailboxTraceEvent_TxRetired
                           : T5AICPMailboxTraceEvent_Error,
            header, 0u, status);
}

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
  ++s_diagnostics.rx_frames;
  prv_trace(T5AICPMailboxTraceEvent_Rx, message->header, message->parameter1, S_SUCCESS);
  if (channel == CP_FLASH_NOTIFY_RX_CHANNEL) {
    if (message->parameter1 == CP_FLASH_NOTIFY_REQUEST) {
      message->parameter1 = CP_FLASH_NOTIFY_ACK;
      ++s_diagnostics.rx_routed;
      prv_trace(T5AICPMailboxTraceEvent_Routed, message->header, message->parameter1, S_SUCCESS);
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
    ++s_diagnostics.rx_routed;
    prv_trace(T5AICPMailboxTraceEvent_Routed, message->header, message->parameter1, S_SUCCESS);
    return;
  }
  if (channel == CP_PM_RX_CHANNEL && prv_enqueue(channel, message)) {
    ++s_diagnostics.rx_routed;
    prv_trace(T5AICPMailboxTraceEvent_Routed, message->header, message->parameter1, S_SUCCESS);
    return;
  }
  ++s_diagnostics.rx_unrouted;
  prv_trace(T5AICPMailboxTraceEvent_Unrouted, message->header, message->parameter1,
            E_INVALID_OPERATION);
  message->header |= 1u << 8u;
}

static void prv_transmit_complete(const T5AIMailboxMessage *ack, status_t status, void *context) {
  (void)context;
  if (PASSED(status)) {
    ++s_diagnostics.tx_retired;
  } else {
    ++s_diagnostics.send_failures;
  }
  prv_tx_completed(ack == NULL ? 0u : ack->header, status);
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
  s_pm_mutex = mutex_create();
  s_log_mutex = mutex_create();
  if (s_control_mutex == NULL || s_pm_mutex == NULL || s_log_mutex == NULL) {
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
  status = t5ai_mailbox_channel_start();
  if (FAILED(status)) {
    return status;
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
  prv_trace(T5AICPMailboxTraceEvent_TxQueued, message.header, message.parameter1, S_SUCCESS);
  status = t5ai_mailbox_channel_send_async(channel, &message, prv_transmit_complete,
                                            (void *)(uintptr_t)channel);
  if (PASSED(status)) {
    ++s_diagnostics.tx_published;
    prv_tx_published(message.header, message.parameter1);
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

static void prv_heartbeat_complete(const T5AIMailboxMessage *ack, status_t status, void *context) {
  (void)context;
  s_heartbeat_in_flight = false;
  if (PASSED(status)) {
    ++s_diagnostics.tx_retired;
  } else {
    ++s_diagnostics.send_failures;
  }
  prv_tx_completed(ack == NULL ? 0u : ack->header, status);
}

void t5ai_cp_mailbox_heartbeat(void) {
  if (!s_runtime_initialized || s_heartbeat_in_flight || t5ai_mailbox_channel_faulted()) {
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
  ++s_diagnostics.tx_queued;
  prv_trace(T5AICPMailboxTraceEvent_TxQueued, s_control_message.header,
            s_control_message.parameter1, S_SUCCESS);
  s_heartbeat_in_flight = true;
  const status_t status = t5ai_mailbox_channel_send_async(
      CP_CONTROL_CHANNEL, &s_control_message, prv_heartbeat_complete, NULL);
  if (FAILED(status)) {
    s_heartbeat_in_flight = false;
    ++s_diagnostics.send_failures;
  } else {
    ++s_diagnostics.tx_published;
    prv_tx_published(s_control_message.header, s_control_message.parameter1);
  }
}

static status_t prv_pm_request(uint8_t command, uint32_t parameter1, uint32_t parameter2) {
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  if (s_pm_faulted) {
    return E_INVALID_OPERATION;
  }
  mutex_lock(s_pm_mutex);
  while (xSemaphoreTake(s_pm_queue.ready, 0u) == pdPASS) {
    T5AICPMailboxFrame stale;
    (void)prv_dequeue(CP_PM_RX_CHANNEL, &stale);
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
    goto out;
  }
  T5AICPMailboxFrame frame;
  if (xSemaphoreTake(s_pm_queue.ready, pdMS_TO_TICKS(CP_PM_TIMEOUT_MS)) != pdPASS ||
      !prv_dequeue(CP_PM_RX_CHANNEL, &frame)) {
    status = E_AGAIN;
    s_pm_faulted = true;
    goto out;
  }
  const T5AIMailboxMessage *response = (const void *)frame.address;
  status = (uint8_t)response->header == command && response->parameter1 == 0u
               ? S_SUCCESS
               : E_ERROR;
out:
  mutex_unlock(s_pm_mutex);
  return status;
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
  return prv_pm_request(CP_PM_CPU_FREQUENCY_COMMAND, CP_PM_CPU1_MODULE, frequency);
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
  if (s_log_faulted) {
    return E_INVALID_OPERATION;
  }
  const bool scheduler_running = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
  if (scheduler_running) {
    status_t status = prv_ensure_runtime();
    if (FAILED(status)) {
      return status;
    }
    if (t5ai_mailbox_channel_faulted()) {
      return E_INVALID_OPERATION;
    }
    mutex_lock(s_log_mutex);
  }
  status_t final_status = prv_wait_for_log_buffer_release(scheduler_running);
  if (FAILED(final_status)) {
    s_log_faulted = true;
    goto out;
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
      final_status = status;
      s_log_faulted = true;
      goto out;
    }
    cursor += chunk;
    length -= chunk;
  }
out:
  if (scheduler_running) {
    mutex_unlock(s_log_mutex);
  }
  return final_status;
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
  s_failure.ticks = CP_AON_RTC_LOW;
  s_failure.tx_queued = s_diagnostics.tx_queued;
  s_failure.tx_published = s_diagnostics.tx_published;
  s_failure.tx_retired = s_diagnostics.tx_retired;
  s_failure.tx_ack_bypass = s_diagnostics.tx_ack_bypass;
  s_failure.rx_frames = s_diagnostics.rx_frames;
  s_failure.rx_transport_acks = s_diagnostics.rx_transport_acks;
  s_failure.rx_routed = s_diagnostics.rx_routed;
  s_failure.rx_unrouted = s_diagnostics.rx_unrouted;
  s_failure.send_failures = s_diagnostics.send_failures;
  s_failure.receive_timeouts = s_diagnostics.receive_timeouts;
  s_failure.tx_ack_mismatches = s_diagnostics.tx_ack_mismatches;
  s_failure.trace_count = s_diagnostics.trace_count < T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY
                              ? s_diagnostics.trace_count
                              : T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY;
  s_failure.tx_depth = s_diagnostics.tx_depth;
  s_failure.tx_high_water = s_diagnostics.tx_high_water;
  const uint8_t first = (uint8_t)((s_trace_next + T5AI_CP_MAILBOX_TRACE_CAPACITY -
                                   s_failure.trace_count) %
                                  T5AI_CP_MAILBOX_TRACE_CAPACITY);
  for (uint32_t i = 0; i < s_failure.trace_count; ++i) {
    s_failure.trace[i] =
        s_diagnostics.trace[(first + i) % T5AI_CP_MAILBOX_TRACE_CAPACITY];
  }
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
