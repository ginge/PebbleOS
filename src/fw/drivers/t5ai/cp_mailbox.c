/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/mcu/cache.h>
#include <pbl/os/mutex.h>
#include <pbl/services/new_timer/new_timer.h>

#include "FreeRTOS.h"
#include "task.h"
#include <cmsis_core.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
#define T5AI_MBOX_INTERRUPT_ENABLE (1u << 8)
#define T5AI_MBOX_CPU_CP 0u
#define T5AI_SYSTEM_CPU1_INT_32_63_ENABLE (*(volatile uint32_t *)0x4401008cu)
#define T5AI_MAILBOX_SYSTEM_INTERRUPT_ENABLE (1u << 31)
#define T5AI_MAILBOX_IRQ ((IRQn_Type)63)

#define T5AI_AON_RTC_LOW (*(volatile uint32_t *)0x4400020cu)
#define T5AI_AON_RTC_HIGH (*(volatile uint32_t *)0x44000228u)
#define T5AI_AON_RTC_HZ 32000u
#define T5AI_MBOX_TIMEOUT_TICKS (2u * T5AI_AON_RTC_HZ)
#define T5AI_MBOX_MAX_ROUTES 4u
#define T5AI_MBOX_TRANSPORT_FRAME_SIZE 16u
#define T5AI_MBOX_TRANSPORT_FRAME_COUNT 8u
#define T5AI_CP_CONTROL_CHANNEL 0x10u
#define T5AI_CP_POWER_UP_COMMAND 1u
#define T5AI_CP_HEARTBEAT_COMMAND 2u
#define T5AI_CP_HEARTBEAT_TICKS T5AI_AON_RTC_HZ
#define T5AI_CP_PM_TX_CHANNEL 0x12u
#define T5AI_CP_PM_RX_CHANNEL 0x42u
#define T5AI_CP_PM_CLOCK_COMMAND 2u
#define T5AI_CP_PM_UART0_CLOCK 2u
#define T5AI_CP_PM_CLOCK_ENABLE 1u
#define T5AI_CP_PM_POWER_COMMAND 1u
#define T5AI_CP_PM_PSRAM_POWER_COMMAND 7u
#define T5AI_CP_PM_QSPI0_CLOCK 20u
#define T5AI_CP_PM_AHBP_QSPI_POWER 101u
#define T5AI_CP_PM_POWER_ON 0u
#define T5AI_CP_PM_PSRAM_APP_MODULE 9u
#define T5AI_CP_PM_SETTLE_TICKS (T5AI_AON_RTC_HZ / 10u)
#define T5AI_CP_HEADER_ACK (1u << 12u)
#define T5AI_CP_ACK_COMPLETE 2u
#define T5AI_CP_ACK_FAIL 3u
#define T5AI_CP_ACK_CAPACITY 4u
#define T5AI_CP_LOG_CHANNEL 0x1eu
#define T5AI_CP_LOG_ASSERT_OUT_COMMAND 4u
#define T5AI_CP_LOG_STATIC_TAG 0xffffu
#define T5AI_CP_LOG_BUFFER_SIZE 160u

typedef struct {
  uint32_t header;
  uint32_t parameter1;
  uint32_t parameter2;
  uint32_t parameter3;
} T5AICPControlMessage;

typedef struct {
  uint8_t logical_channel;
  T5AICPMailboxReceiveCallback callback;
  void *context;
} T5AICPMailboxRoute;

static PebbleMutex *s_mutex;
static T5AICPMailboxRoute s_routes[T5AI_MBOX_MAX_ROUTES];
static uint8_t s_transport_frames[T5AI_MBOX_TRANSPORT_FRAME_COUNT][T5AI_MBOX_TRANSPORT_FRAME_SIZE]
    __attribute__((aligned(32)));
static T5AICPControlMessage s_control_message __attribute__((aligned(32)));
static T5AICPControlMessage s_pm_message __attribute__((aligned(32)));
static T5AICPControlMessage s_ack_message __attribute__((aligned(32)));
static T5AICPControlMessage s_logical_ack_messages[T5AI_CP_ACK_CAPACITY]
    __attribute__((aligned(32)));
static T5AICPControlMessage s_log_message __attribute__((aligned(32)));
static uint8_t s_log_buffer[T5AI_CP_LOG_BUFFER_SIZE] __attribute__((aligned(32)));
static uint32_t s_heartbeat_parameter __attribute__((aligned(32)));
static uint8_t s_control_sequence;
static uint8_t s_transport_frame_index;
static uint8_t s_logical_ack_index;
static uint64_t s_last_heartbeat;
static volatile bool s_rx_interrupt_work_pending;

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
  return prv_ticks() - start >= T5AI_MBOX_TIMEOUT_TICKS;
}

static bool prv_dispatch(uint8_t logical_channel, const T5AICPMailboxFrame *frame) {
  for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
    if (s_routes[i].callback != NULL && s_routes[i].logical_channel == logical_channel) {
      s_routes[i].callback(logical_channel, frame, s_routes[i].context);
      return true;
    }
  }
  return false;
}

static bool prv_is_pm_response(const T5AICPControlMessage *message, uint8_t command) {
  return (message->header >> 24u) == T5AI_CP_PM_RX_CHANNEL && (uint8_t)message->header == command &&
         message->parameter1 == 0u;
}

static bool prv_is_transport_ack(const T5AICPControlMessage *message) {
  return (message->header & T5AI_CP_HEADER_ACK) != 0u;
}

static status_t prv_acknowledge(const T5AICPControlMessage *message) {
  s_ack_message = *message;
  s_ack_message.header = (s_ack_message.header & ~(0xfu << 8u)) | T5AI_CP_HEADER_ACK;
  dcache_flush(&s_ack_message, sizeof(s_ack_message));
  return t5ai_cp_mailbox_send(&s_ack_message, sizeof(s_ack_message));
}

static status_t prv_reject_unrouted(const T5AICPMailboxFrame *frame) {
  if (frame->length < sizeof(s_ack_message)) {
    return E_ERROR;
  }
  dcache_invalidate((void *)frame->address, sizeof(s_ack_message));
  memcpy(&s_ack_message, (const void *)frame->address, sizeof(s_ack_message));
  s_ack_message.header = (s_ack_message.header & ~(0xfu << 8u)) | T5AI_CP_HEADER_ACK | (1u << 8u);
  dcache_flush(&s_ack_message, sizeof(s_ack_message));
  return t5ai_cp_mailbox_send(&s_ack_message, sizeof(s_ack_message));
}

status_t t5ai_cp_mailbox_lock(void) {
  if (s_mutex == NULL) {
    /* Flash initializes the mailbox before concurrent clients are started. */
    s_mutex = mutex_create();
  }
  PebbleMutex *mutex = s_mutex;
  if (mutex == NULL) {
    return E_OUT_OF_MEMORY;
  }
  mutex_lock(mutex);
  return S_SUCCESS;
}

void t5ai_cp_mailbox_unlock(void) {
  if (s_mutex != NULL) {
    mutex_unlock(s_mutex);
  }
}

static status_t prv_send(const void *data, size_t length, bool wait_for_space) {
  if (data == NULL || length == 0 || length > T5AI_MBOX_TRANSPORT_FRAME_SIZE) {
    return E_INVALID_ARGUMENT;
  }
  const uint64_t start = prv_ticks();
  while ((T5AI_MBOX_CH0_FIFO_STATUS & T5AI_MBOX_FIFO_FULL) != 0) {
    if (!wait_for_space) {
      return E_BUSY;
    }
    if (prv_timed_out(start)) {
      return E_BUSY;
    }
  }
  uint8_t *frame = s_transport_frames[s_transport_frame_index];
  s_transport_frame_index =
      (uint8_t)((s_transport_frame_index + 1u) % T5AI_MBOX_TRANSPORT_FRAME_COUNT);
  memset(frame, 0, T5AI_MBOX_TRANSPORT_FRAME_SIZE);
  memcpy(frame, data, length);
  dcache_flush(frame, T5AI_MBOX_TRANSPORT_FRAME_SIZE);
  T5AI_MBOX_CH1_TX_DATA0 = (uint32_t)(uintptr_t)frame;
  T5AI_MBOX_CH1_TX_DATA1 = (uint32_t)length;
  T5AI_MBOX_CH1_TX_ID = T5AI_MBOX_CPU_CP;
  return S_SUCCESS;
}

status_t t5ai_cp_mailbox_send(const void *data, size_t length) {
  return prv_send(data, length, true);
}

static status_t prv_receive_physical(T5AICPMailboxFrame *frame, uint64_t start) {
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) == 0) {
    if (prv_timed_out(start)) {
      return E_AGAIN;
    }
  }
  (void)T5AI_MBOX_CH1_RX_ID;
  frame->address = T5AI_MBOX_CH1_RX_DATA0;
  frame->length = T5AI_MBOX_CH1_RX_DATA1;
  return frame->address != 0 && frame->length != 0 ? S_SUCCESS : E_ERROR;
}

static status_t prv_frame_channel(const T5AICPMailboxFrame *frame, uint8_t *logical_channel) {
  if (frame->address == 0 || frame->length < sizeof(uint32_t)) {
    return E_ERROR;
  }
  uint32_t header;
  dcache_invalidate((void *)frame->address, sizeof(header));
  memcpy(&header, (const void *)frame->address, sizeof(header));
  *logical_channel = (uint8_t)(header >> 24);
  return S_SUCCESS;
}

static bool prv_frame_is_transport_ack(const T5AICPMailboxFrame *frame) {
  if (frame->address == 0 || frame->length < sizeof(uint32_t)) {
    return false;
  }
  uint32_t header;
  dcache_invalidate((void *)frame->address, sizeof(header));
  memcpy(&header, (const void *)frame->address, sizeof(header));
  return (header & T5AI_CP_HEADER_ACK) != 0u;
}

status_t t5ai_cp_mailbox_register(uint8_t logical_channel, T5AICPMailboxReceiveCallback callback,
                                  void *context) {
  if (callback == NULL) {
    return E_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
    if (s_routes[i].callback != NULL && s_routes[i].logical_channel == logical_channel) {
      return E_INVALID_OPERATION;
    }
  }
  for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
    if (s_routes[i].callback == NULL) {
      s_routes[i] = (T5AICPMailboxRoute){
          .logical_channel = logical_channel,
          .callback = callback,
          .context = context,
      };
      return S_SUCCESS;
    }
  }
  return E_OUT_OF_RESOURCES;
}

void t5ai_cp_mailbox_unregister(uint8_t logical_channel) {
  for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
    if (s_routes[i].callback != NULL && s_routes[i].logical_channel == logical_channel) {
      memset(&s_routes[i], 0, sizeof(s_routes[i]));
      return;
    }
  }
}

status_t t5ai_cp_mailbox_acknowledge(uint32_t header, uint32_t parameter1, uint32_t parameter2,
                                     bool handled) {
  T5AICPControlMessage *ack = &s_logical_ack_messages[s_logical_ack_index];
  s_logical_ack_index = (uint8_t)((s_logical_ack_index + 1u) % T5AI_CP_ACK_CAPACITY);
  *ack = (T5AICPControlMessage){
      .header = (header & ~(0xfu << 8u)) | T5AI_CP_HEADER_ACK,
      .parameter1 = parameter1,
      .parameter2 = parameter2,
  };
  ack->parameter3 = handled ? T5AI_CP_ACK_COMPLETE : T5AI_CP_ACK_FAIL;
  dcache_flush(ack, sizeof(*ack));
  return t5ai_cp_mailbox_send(ack, sizeof(*ack));
}

static status_t prv_process_pending_locked(void) {
  status_t status = S_SUCCESS;
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) != 0u) {
    (void)T5AI_MBOX_CH1_RX_ID;
    T5AICPMailboxFrame frame = {
        .address = T5AI_MBOX_CH1_RX_DATA0,
        .length = T5AI_MBOX_CH1_RX_DATA1,
    };
    uint8_t logical_channel;
    status = prv_frame_channel(&frame, &logical_channel);
    if (FAILED(status)) {
      break;
    }
    if (prv_frame_is_transport_ack(&frame) || prv_dispatch(logical_channel, &frame)) {
      continue;
    }
    status = prv_reject_unrouted(&frame);
    if (FAILED(status)) {
      break;
    }
  }

  return status;
}

status_t t5ai_cp_mailbox_process_pending(void) {
  PebbleMutex *mutex = s_mutex;
  if (mutex == NULL || !mutex_lock_with_timeout(mutex, 0u)) {
    return E_BUSY;
  }
  const status_t status = prv_process_pending_locked();
  mutex_unlock(mutex);
  return status;
}

static void prv_process_pending_from_interrupt(void *context) {
  (void)context;
  PebbleMutex *mutex = s_mutex;
  if (mutex != NULL) {
    mutex_lock(mutex);
    (void)prv_process_pending_locked();
    mutex_unlock(mutex);
  }
  __atomic_store_n(&s_rx_interrupt_work_pending, false, __ATOMIC_RELEASE);
  T5AI_MBOX_CH1_CTRL |= T5AI_MBOX_INTERRUPT_ENABLE;
}

void MAILBOX_IRQHandler(void) {
  T5AI_MBOX_CH1_CTRL &= ~T5AI_MBOX_INTERRUPT_ENABLE;
  bool should_context_switch = false;
  if (!__atomic_exchange_n(&s_rx_interrupt_work_pending, true, __ATOMIC_ACQ_REL)) {
    should_context_switch =
        new_timer_add_work_callback_from_isr(prv_process_pending_from_interrupt, NULL);
  }
  portEND_SWITCHING_ISR(should_context_switch);
}

void t5ai_cp_mailbox_enable_rx_interrupt(void) {
  T5AI_MBOX_CH1_CTRL |= T5AI_MBOX_INTERRUPT_ENABLE;
  T5AI_SYSTEM_CPU1_INT_32_63_ENABLE |= T5AI_MAILBOX_SYSTEM_INTERRUPT_ENABLE;
  NVIC_SetPriority(T5AI_MAILBOX_IRQ, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_EnableIRQ(T5AI_MAILBOX_IRQ);
}

status_t t5ai_cp_mailbox_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame) {
  if (frame == NULL) {
    return E_INVALID_ARGUMENT;
  }
  const uint64_t start = prv_ticks();
  for (;;) {
    status_t status = prv_receive_physical(frame, start);
    uint8_t received_channel;
    if (FAILED(status)) {
      return status;
    }
    if (FAILED(prv_frame_channel(frame, &received_channel))) {
      return E_ERROR;
    }
    if (received_channel == logical_channel) {
      return S_SUCCESS;
    }
    if (prv_dispatch(received_channel, frame)) {
      continue;
    }
    if (prv_frame_is_transport_ack(frame)) {
      continue;
    }
    status = prv_reject_unrouted(frame);
    if (FAILED(status)) {
      return status;
    }
  }
}

status_t t5ai_cp_mailbox_try_receive(uint8_t logical_channel, T5AICPMailboxFrame *frame) {
  if (frame == NULL) {
    return E_INVALID_ARGUMENT;
  }
  if ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) == 0) {
    return E_AGAIN;
  }
  for (;;) {
    (void)T5AI_MBOX_CH1_RX_ID;
    frame->address = T5AI_MBOX_CH1_RX_DATA0;
    frame->length = T5AI_MBOX_CH1_RX_DATA1;
    uint8_t received_channel;
    if (FAILED(prv_frame_channel(frame, &received_channel))) {
      return E_ERROR;
    }
    if (received_channel == logical_channel) {
      return S_SUCCESS;
    }
    if (!prv_dispatch(received_channel, frame)) {
      if (prv_frame_is_transport_ack(frame)) {
        if ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) == 0) {
          return E_AGAIN;
        }
        continue;
      }
      status_t status = prv_reject_unrouted(frame);
      if (FAILED(status)) {
        return status;
      }
    }
    if ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) == 0) {
      return E_AGAIN;
    }
  }
}

void t5ai_cp_mailbox_discard_pending(void) {
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) != 0) {
    (void)T5AI_MBOX_CH1_RX_ID;
    (void)T5AI_MBOX_CH1_RX_DATA0;
    (void)T5AI_MBOX_CH1_RX_DATA1;
  }
}

static status_t prv_send_control_with_policy(uint8_t command, const void *buffer, size_t length,
                                             bool wait_for_space) {
  ++s_control_sequence;
  s_control_message.header =
      command | ((uint32_t)s_control_sequence << 16) | (T5AI_CP_CONTROL_CHANNEL << 24);
  s_control_message.parameter1 = (uint32_t)(uintptr_t)buffer;
  s_control_message.parameter2 = (uint32_t)length;
  s_control_message.parameter3 = 0u;
  if (buffer != NULL && length != 0u) {
    dcache_flush((void *)buffer, length);
  }
  dcache_flush(&s_control_message, sizeof(s_control_message));
  return prv_send(&s_control_message, sizeof(s_control_message), wait_for_space);
}

static status_t prv_send_control(uint8_t command, const void *buffer, size_t length) {
  return prv_send_control_with_policy(command, buffer, length, true);
}

static status_t prv_control_request(uint8_t command, const void *buffer, size_t length) {
  status_t status = prv_send_control(command, buffer, length);
  if (FAILED(status)) {
    return status;
  }

  const uint64_t start = prv_ticks();
  for (;;) {
    T5AICPMailboxFrame frame;
    status = prv_receive_physical(&frame, start);
    if (FAILED(status) || frame.length < sizeof(T5AICPControlMessage)) {
      return FAILED(status) ? status : E_ERROR;
    }

    T5AICPControlMessage response;
    dcache_invalidate((void *)frame.address, sizeof(response));
    memcpy(&response, (const void *)frame.address, sizeof(response));
    if ((uint8_t)(response.header >> 24u) != T5AI_CP_CONTROL_CHANNEL) {
      if (prv_is_transport_ack(&response)) {
        continue;
      }
      if (prv_dispatch((uint8_t)(response.header >> 24u), &frame)) {
        continue;
      }
      status = prv_reject_unrouted(&frame);
      if (FAILED(status)) {
        return status;
      }
      continue;
    }
    if (prv_is_transport_ack(&response)) {
      if ((uint8_t)(response.header >> 16u) == s_control_sequence) {
        return (response.header & (0xfu << 8u)) == 0u ? S_SUCCESS : E_ERROR;
      }
      continue;
    }

    status = prv_acknowledge(&response);
    if (FAILED(status)) {
      return status;
    }
  }
}

void t5ai_cp_mailbox_announce_power_up(void) {
  (void)prv_control_request(T5AI_CP_POWER_UP_COMMAND, NULL, 0u);
  s_last_heartbeat = prv_ticks();
}

void t5ai_cp_mailbox_heartbeat(void) {
  const uint64_t now = prv_ticks();
  if (now - s_last_heartbeat < T5AI_CP_HEARTBEAT_TICKS) {
    return;
  }
  PebbleMutex *mutex = s_mutex;
  if (mutex == NULL || !mutex_lock_with_timeout(mutex, 0u)) {
    return;
  }
  s_heartbeat_parameter = 0u;
  const status_t status = prv_send_control_with_policy(
      T5AI_CP_HEARTBEAT_COMMAND, &s_heartbeat_parameter, sizeof(s_heartbeat_parameter), false);
  mutex_unlock(mutex);
  if (PASSED(status)) {
    s_last_heartbeat = now;
  }
}

static status_t prv_pm_request(uint8_t command, uint32_t parameter1, uint32_t parameter2) {
  ++s_control_sequence;
  s_pm_message.header =
      command | ((uint32_t)s_control_sequence << 16) | (T5AI_CP_PM_TX_CHANNEL << 24);
  s_pm_message.parameter1 = parameter1;
  s_pm_message.parameter2 = parameter2;
  s_pm_message.parameter3 = 0u;
  dcache_flush(&s_pm_message, sizeof(s_pm_message));

  status_t status = t5ai_cp_mailbox_send(&s_pm_message, sizeof(s_pm_message));
  if (FAILED(status)) {
    return status;
  }

  T5AICPMailboxFrame response_frame;
  const uint64_t start = prv_ticks();
  while (1) {
    status = t5ai_cp_mailbox_try_receive(T5AI_CP_PM_RX_CHANNEL, &response_frame);
    if (status == S_SUCCESS) {
      if (response_frame.length < sizeof(T5AICPControlMessage)) {
        return E_ERROR;
      }
      T5AICPControlMessage response;
      dcache_invalidate((void *)response_frame.address, sizeof(response));
      memcpy(&response, (const void *)response_frame.address, sizeof(response));
      if (prv_is_transport_ack(&response)) {
        continue;
      }
      status = prv_acknowledge(&response);
      if (FAILED(status)) {
        return status;
      }
      return prv_is_pm_response(&response, command) ? S_SUCCESS : E_ERROR;
    }
    if (status != E_AGAIN) {
      return status;
    }
    if (prv_ticks() - start >= T5AI_CP_PM_SETTLE_TICKS) {
      return E_AGAIN;
    }
  }
}

status_t t5ai_cp_mailbox_enable_uart0_clock(void) {
  return prv_pm_request(T5AI_CP_PM_CLOCK_COMMAND, T5AI_CP_PM_UART0_CLOCK, T5AI_CP_PM_CLOCK_ENABLE);
}

status_t t5ai_cp_mailbox_enable_qspi0(void) {
  status_t status =
      prv_pm_request(T5AI_CP_PM_POWER_COMMAND, T5AI_CP_PM_AHBP_QSPI_POWER, T5AI_CP_PM_POWER_ON);
  return FAILED(status) ? status
                        : prv_pm_request(T5AI_CP_PM_CLOCK_COMMAND, T5AI_CP_PM_QSPI0_CLOCK,
                                         T5AI_CP_PM_CLOCK_ENABLE);
}

status_t t5ai_cp_mailbox_enable_psram(void) {
  return prv_pm_request(T5AI_CP_PM_PSRAM_POWER_COMMAND, T5AI_CP_PM_PSRAM_APP_MODULE,
                        T5AI_CP_PM_POWER_ON);
}

status_t t5ai_cp_mailbox_log(const void *data, size_t length) {
  if (data == NULL || length == 0u) {
    return E_INVALID_ARGUMENT;
  }

  const uint8_t *cursor = data;
  while (length != 0u) {
    const size_t chunk_length =
        length < sizeof(s_log_buffer) - 1u ? length : sizeof(s_log_buffer) - 1u;
    s_log_buffer[0] = 1u;
    memcpy(&s_log_buffer[1], cursor, chunk_length);

    ++s_control_sequence;
    s_log_message.header = T5AI_CP_LOG_ASSERT_OUT_COMMAND | ((uint32_t)s_control_sequence << 16) |
                           (T5AI_CP_LOG_CHANNEL << 24);
    s_log_message.parameter1 = (uint32_t)(uintptr_t)s_log_buffer;
    s_log_message.parameter2 = (uint32_t)(chunk_length + 1u) | (T5AI_CP_LOG_STATIC_TAG << 16);
    s_log_message.parameter3 = 0u;
    dcache_flush(s_log_buffer, chunk_length + 1u);
    dcache_flush(&s_log_message, sizeof(s_log_message));

    status_t status = t5ai_cp_mailbox_send(&s_log_message, sizeof(s_log_message));
    if (FAILED(status)) {
      return status;
    }

    const uint64_t start = prv_ticks();
    do {
      dcache_invalidate(s_log_buffer, 1u);
      if (s_log_buffer[0] == 0u) {
        break;
      }
      if (prv_timed_out(start)) {
        return E_AGAIN;
      }
    } while (true);

    cursor += chunk_length;
    length -= chunk_length;
  }
  return S_SUCCESS;
}
