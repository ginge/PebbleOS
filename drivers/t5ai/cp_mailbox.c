/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/kernel.h>
#include <pbl/kernel/time.h>
#include <pbl/mcu/cache.h>
#include <pbl/mcu/interrupts.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/rtc.h>

#define CP_LOG_TAG_SHIFT 16u

#define CP_CONTROL_CHANNEL 0x10u
#define CP_CONTROL_RX_CHANNEL 0x40u
#define CP_RESPONSE_FLAG 0x80u
#define CP_ACK_STATE_PENDING 1u
#define CP_ACK_STATE_COMPLETE 2u
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
#define CP_PM_AUDP_AUDIO_POWER 122u
#define CP_PM_AUDIO_CLOCK 30u
#define CP_PM_POWER_ON 0u
#define CP_PM_PSRAM_APP_MODULE 9u
#define CP_PM_SLEEP_APP_MODULE 12u
#define CP_PM_CPU1_MODULE 38u
#define CP_LOG_CHANNEL 0x1eu
#define CP_LOG_ASSERT_OUT_COMMAND 4u
#define CP_LOG_STATIC_TAG 0xffffu
#define CP_LOG_BUFFER_SIZE 160u
#define CP_FLASH_NOTIFY_RX_CHANNEL 0x4bu
#define CP_FLASH_NOTIFY_REQUEST 1u
#define CP_FLASH_NOTIFY_ACK 2u
#define CP_RUNTIME_TIMEOUT_MS 5000u
#define CP_HEARTBEAT_PERIOD_MS 1000u
#define CP_PM_TIMEOUT_MS 100u
#define CP_STARTUP_TIMEOUT_MS 2000u
#define CP_QUEUE_DEPTH 4u

typedef struct {
  T5AICPMailboxFrame frames[CP_QUEUE_DEPTH];
  uint8_t head;
  uint8_t count;
  struct pbl_sem ready;
} ReceiveQueue;

typedef struct {
  T5AICPMailboxReceiveCallback callback;
  void *context;
} Route;

static Route s_routes[T5AI_MAILBOX_CHANNEL_COUNT];
/* Only the PM protocol has a blocking compatibility consumer. Routed clients
 * (MIPC, HCI, flash notifications, and logging) own their receive state and
 * must not allocate dormant per-channel queues. */
static ReceiveQueue s_pm_queue = {.ready = PBL_SEM_INITIALIZER(0, CP_QUEUE_DEPTH)};
static PBL_MUTEX_DEFINE(s_control_mutex);
static PBL_MUTEX_DEFINE(s_pm_mutex);
static PBL_MUTEX_DEFINE(s_log_mutex);
static T5AIMailboxMessage s_control_message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static T5AIMailboxMessage s_pm_message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static T5AIMailboxMessage s_log_message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint8_t s_log_buffer[CP_LOG_BUFFER_SIZE] __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint32_t s_heartbeat_parameter __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static uint8_t s_control_sequence;
static bool s_heartbeat_in_flight;
static uint64_t s_next_heartbeat_ticks;
static bool s_runtime_initialized;
static bool s_pm_faulted;
static bool s_log_faulted;

bool t5ai_cp_mailbox_shared_range_valid(uintptr_t address, size_t length) {
  if (length == 0u || address > UINTPTR_MAX - length) {
    return false;
  }
  const uintptr_t end = address + length;
  return (address >= T5AI_SHARED_SRAM_BEGIN && end <= T5AI_SHARED_SRAM_END) ||
         (address >= T5AI_SHARED_PSRAM_BEGIN && end <= T5AI_SHARED_PSRAM_END);
}

static bool prv_enqueue(uint8_t channel, const T5AIMailboxMessage *message) {
  if (channel != CP_PM_RX_CHANNEL) {
    return false;
  }
  ReceiveQueue *queue = &s_pm_queue;
  pbl_irq_lock();
  if (queue->count == CP_QUEUE_DEPTH) {
    pbl_irq_unlock();
    return false;
  }
  const uint8_t tail = (uint8_t)((queue->head + queue->count) % CP_QUEUE_DEPTH);
  T5AICPMailboxFrame *frame = &queue->frames[tail];
  memcpy(frame->snapshot, message, sizeof(*message));
  frame->address = (uintptr_t)frame->snapshot;
  frame->length = sizeof(*message);
  ++queue->count;
  pbl_irq_unlock();
  (void)pbl_sem_give(&queue->ready);
  return true;
}

static bool prv_dequeue(uint8_t channel, T5AICPMailboxFrame *frame) {
  if (channel != CP_PM_RX_CHANNEL) {
    return false;
  }
  ReceiveQueue *queue = &s_pm_queue;
  pbl_irq_lock();
  if (queue->count == 0u) {
    pbl_irq_unlock();
    return false;
  }
  *frame = queue->frames[queue->head];
  memcpy(frame->snapshot, (const void *)frame->address, frame->length);
  frame->address = (uintptr_t)frame->snapshot;
  queue->head = (uint8_t)((queue->head + 1u) % CP_QUEUE_DEPTH);
  --queue->count;
  pbl_irq_unlock();
  return true;
}

static void prv_receive(T5AIMailboxMessage *message, void *context) {
  const uint8_t channel = (uint8_t)(uintptr_t)context;
  const uint8_t index = channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK;
  /* A pending control command may complete through a separate response. */
  if (channel == CP_CONTROL_RX_CHANNEL &&
      (uint8_t)message->header == (CP_HEARTBEAT_COMMAND | CP_RESPONSE_FLAG) &&
      s_heartbeat_in_flight) {
    t5ai_reboot_trace_event(T5RebootTrace_HeartbeatResponse, 0u);
    s_heartbeat_in_flight = false;
    message->parameter1 = 0u;
    message->parameter2 = 0u;
    message->parameter3 = CP_ACK_STATE_COMPLETE;
    return;
  }
  /* This compatibility ACK does not park AP execution or prevent XIP reads.
   * CP code must not use it as exclusive ownership for a flash-to-SPI mux. */
  if (channel == CP_FLASH_NOTIFY_RX_CHANNEL) {
    if (message->parameter1 == CP_FLASH_NOTIFY_REQUEST) {
      message->parameter1 = CP_FLASH_NOTIFY_ACK;
      return;
    }
    message->header |= T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED;
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
  message->header |= T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED;
}

static void prv_transmit_complete(const T5AIMailboxMessage *ack, status_t status, void *context) {
  (void)ack;
  (void)status;
  (void)context;
}

static status_t prv_ensure_runtime(void) {
  if (__atomic_load_n(&s_runtime_initialized, __ATOMIC_ACQUIRE)) {
    return S_SUCCESS;
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
  __atomic_store_n(&s_runtime_initialized, true, __ATOMIC_RELEASE);
  t5ai_mailbox_channel_wake();
  return S_SUCCESS;
}

status_t t5ai_cp_mailbox_lock(void) {
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  pbl_mutex_lock(&s_control_mutex, PBL_FOREVER);
  return S_SUCCESS;
}

void t5ai_cp_mailbox_unlock(void) {
  if (__atomic_load_n(&s_runtime_initialized, __ATOMIC_ACQUIRE)) {
    pbl_mutex_unlock(&s_control_mutex);
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
  const uint8_t channel = (uint8_t)(message.header >> T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
  status = t5ai_mailbox_channel_send_async(channel, &message, prv_transmit_complete,
                                            (void *)(uintptr_t)channel);
  return status;
}

status_t t5ai_cp_mailbox_send_priority(const void *data, size_t length) {
  return t5ai_cp_mailbox_send(data, length);
}

status_t t5ai_cp_mailbox_register(uint8_t logical_channel, T5AICPMailboxReceiveCallback callback,
                                  void *context) {
  if ((logical_channel & T5AI_MAILBOX_CHANNEL_BASE_MASK) != T5AI_MAILBOX_RX_CHANNEL_BASE ||
      callback == NULL) {
    return E_INVALID_ARGUMENT;
  }
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  const uint8_t index = logical_channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK;
  pbl_irq_lock();
  if (s_routes[index].callback != NULL) {
    pbl_irq_unlock();
    return E_INVALID_OPERATION;
  }
  s_routes[index] = (Route){.callback = callback, .context = context};
  pbl_irq_unlock();
  return S_SUCCESS;
}

void t5ai_cp_mailbox_unregister(uint8_t logical_channel) {
  if ((logical_channel & T5AI_MAILBOX_CHANNEL_BASE_MASK) != T5AI_MAILBOX_RX_CHANNEL_BASE) {
    return;
  }
  pbl_irq_lock();
  s_routes[logical_channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK] = (Route){0};
  pbl_irq_unlock();
}

status_t t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *frame, bool handled) {
  if (frame == NULL || frame->address == 0u || frame->length != sizeof(T5AIMailboxMessage)) {
    return E_INVALID_ARGUMENT;
  }
  T5AIMailboxMessage *message = (T5AIMailboxMessage *)(uintptr_t)frame->address;
  if (!handled) {
    message->header |= T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED;
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
  if (pbl_sem_take(&s_pm_queue.ready, PBL_TICKS(pbl_ms_to_ticks(CP_RUNTIME_TIMEOUT_MS))) != 0) {
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
  if (logical_channel != CP_PM_RX_CHANNEL || pbl_sem_take(&s_pm_queue.ready, PBL_TICKS(0u)) != 0) {
    return E_AGAIN;
  }
  return prv_dequeue(logical_channel, frame) ? S_SUCCESS : E_ERROR;
}

void t5ai_cp_mailbox_discard_pending(void) {
  if (!__atomic_load_n(&s_runtime_initialized, __ATOMIC_ACQUIRE)) {
    return;
  }
  pbl_irq_lock();
  s_pm_queue.head = 0u;
  s_pm_queue.count = 0u;
  pbl_irq_unlock();
  while (pbl_sem_take(&s_pm_queue.ready, PBL_TICKS(0u)) == 0) {
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
  t5ai_reboot_trace_event(T5RebootTrace_HeartbeatAck,
                         FAILED(status) ? (uint32_t)status : (ack ? ack->parameter3 : UINT32_MAX));
  /* Tuya normally completes heartbeat processing in the physical ACK. */
  if (PASSED(status) && ack != NULL && ack->parameter3 == CP_ACK_STATE_COMPLETE) {
    s_heartbeat_in_flight = false;
  } else if (FAILED(status) || ack == NULL || ack->parameter3 != CP_ACK_STATE_PENDING) {
    s_heartbeat_in_flight = false;
  }
}

static void prv_heartbeat(void) {
  t5ai_reboot_trace_event(T5RebootTrace_HeartbeatAttempt, 0u);
  if (!__atomic_load_n(&s_runtime_initialized, __ATOMIC_ACQUIRE) || s_heartbeat_in_flight ||
      t5ai_mailbox_channel_faulted()) {
    t5ai_reboot_trace_event(T5RebootTrace_HeartbeatSkip,
                            (!__atomic_load_n(&s_runtime_initialized, __ATOMIC_ACQUIRE) ? 1u : 0u) |
                                (s_heartbeat_in_flight ? 2u : 0u) |
                           (t5ai_mailbox_channel_faulted() ? 4u : 0u));
    return;
  }
  s_heartbeat_parameter = 0u;
  ++s_control_sequence;
  s_control_message = (T5AIMailboxMessage){
      .header = CP_HEARTBEAT_COMMAND |
                ((uint32_t)s_control_sequence << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) |
                ((uint32_t)CP_CONTROL_CHANNEL << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT),
      .parameter1 = (uint32_t)(uintptr_t)&s_heartbeat_parameter,
      .parameter2 = sizeof(s_heartbeat_parameter),
  };
  dcache_flush(&s_heartbeat_parameter, sizeof(s_heartbeat_parameter));
  s_heartbeat_in_flight = true;
  const status_t status = t5ai_mailbox_channel_send_async(CP_CONTROL_CHANNEL, &s_control_message,
                                                          prv_heartbeat_complete, NULL);
  t5ai_reboot_trace_event(T5RebootTrace_HeartbeatSend, (uint32_t)status);
  if (FAILED(status)) {
    s_heartbeat_in_flight = false;
  }
}

pbl_tick_t t5ai_cp_mailbox_heartbeat_poll(void) {
  if (!__atomic_load_n(&s_runtime_initialized, __ATOMIC_ACQUIRE)) {
    return PBL_TICK_FOREVER;
  }
  const uint64_t now = pbl_uptime_ticks();
  if (now < s_next_heartbeat_ticks) {
    return (pbl_tick_t)(s_next_heartbeat_ticks - now);
  }
  prv_heartbeat();
  const pbl_tick_t period = pbl_ms_to_ticks(CP_HEARTBEAT_PERIOD_MS);
  // Late execution skips missed periods instead of bursting or underflowing.
  s_next_heartbeat_ticks = pbl_uptime_ticks() + period;
  return period;
}

static status_t prv_pm_request(uint8_t command, uint32_t parameter1, uint32_t parameter2) {
  status_t status = prv_ensure_runtime();
  if (FAILED(status)) {
    return status;
  }
  if (s_pm_faulted) {
    return E_INVALID_OPERATION;
  }
  pbl_mutex_lock(&s_pm_mutex, PBL_FOREVER);
  while (pbl_sem_take(&s_pm_queue.ready, PBL_TICKS(0u)) == 0) {
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
  if (pbl_sem_take(&s_pm_queue.ready, PBL_TICKS(pbl_ms_to_ticks(CP_PM_TIMEOUT_MS))) != 0 ||
      !prv_dequeue(CP_PM_RX_CHANNEL, &frame)) {
    status = E_AGAIN;
    s_pm_faulted = true;
    goto out;
  }
  const T5AIMailboxMessage *response = (const void *)frame.address;
  status = (uint8_t)response->header == command && response->parameter1 == 0u ? S_SUCCESS : E_ERROR;
out:
  pbl_mutex_unlock(&s_pm_mutex);
  return status;
}

status_t t5ai_cp_mailbox_enable_uart0_clock(void) {
  return prv_pm_request(CP_PM_CLOCK_COMMAND, CP_PM_UART0_CLOCK, CP_PM_CLOCK_ENABLE);
}

status_t t5ai_cp_mailbox_enable_qspi0(void) {
  status_t status = prv_pm_request(CP_PM_POWER_COMMAND, CP_PM_AHBP_QSPI_POWER, CP_PM_POWER_ON);
  return FAILED(status)
             ? status
             : prv_pm_request(CP_PM_CLOCK_COMMAND, CP_PM_QSPI0_CLOCK, CP_PM_CLOCK_ENABLE);
}

status_t t5ai_cp_mailbox_enable_psram(void) {
  return prv_pm_request(CP_PM_PSRAM_POWER_COMMAND, CP_PM_PSRAM_APP_MODULE, CP_PM_POWER_ON);
}

status_t t5ai_cp_mailbox_set_audio_power(bool enabled) {
  return prv_pm_request(CP_PM_POWER_COMMAND, CP_PM_AUDP_AUDIO_POWER, enabled ? 0u : 1u);
}

status_t t5ai_cp_mailbox_set_audio_clock(bool enabled) {
  return prv_pm_request(CP_PM_CLOCK_COMMAND, CP_PM_AUDIO_CLOCK, enabled ? 1u : 0u);
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
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t timeout_ticks = CP_RUNTIME_TIMEOUT_MS * (T5AI_AON_RTC_HZ / 1000u);
  do {
    dcache_invalidate(s_log_buffer, 1u);
    if (s_log_buffer[0] == 0u) {
      return S_SUCCESS;
    }
    if (scheduler_running) {
      pbl_thread_sleep(PBL_MSEC(1u));
    }
  } while ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < timeout_ticks);
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
  const bool scheduler_running = pbl_kernel_is_running();
  if (scheduler_running) {
    status_t status = prv_ensure_runtime();
    if (FAILED(status)) {
      return status;
    }
    if (t5ai_mailbox_channel_faulted()) {
      return E_INVALID_OPERATION;
    }
    pbl_mutex_lock(&s_log_mutex, PBL_FOREVER);
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
        .parameter2 = (uint32_t)(chunk + 1u) | (CP_LOG_STATIC_TAG << CP_LOG_TAG_SHIFT),
    };
    dcache_flush(s_log_buffer, chunk + 1u);
    T5AIMailboxMessage ack;
    status_t status =
        scheduler_running
            ? t5ai_mailbox_channel_send(CP_LOG_CHANNEL, &s_log_message, &ack, CP_RUNTIME_TIMEOUT_MS)
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
    pbl_mutex_unlock(&s_log_mutex);
  }
  return final_status;
}
