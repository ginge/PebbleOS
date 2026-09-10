/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/mcu/cache.h>
#include <pbl/os/mutex.h>

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
#define T5AI_MBOX_CPU_CP 0u

#define T5AI_AON_RTC_LOW (*(volatile uint32_t *)0x4400020cu)
#define T5AI_AON_RTC_HIGH (*(volatile uint32_t *)0x44000228u)
#define T5AI_AON_RTC_HZ 32000u
#define T5AI_MBOX_TIMEOUT_TICKS (2u * T5AI_AON_RTC_HZ)
#define T5AI_MBOX_MAX_ROUTES 4u
#define T5AI_CP_CONTROL_CHANNEL 0x10u
#define T5AI_CP_POWER_UP_COMMAND 1u
#define T5AI_CP_HEARTBEAT_COMMAND 2u
#define T5AI_CP_HEARTBEAT_TICKS T5AI_AON_RTC_HZ

typedef struct {
  uint32_t header;
  uint32_t buffer;
  uint32_t length;
  uint32_t reserved;
} T5AICPControlMessage;

typedef struct {
  uint8_t logical_channel;
  T5AICPMailboxReceiveCallback callback;
  void *context;
} T5AICPMailboxRoute;

static PebbleMutex *s_mutex;
static T5AICPMailboxRoute s_routes[T5AI_MBOX_MAX_ROUTES];
static T5AICPControlMessage s_control_message __attribute__((aligned(32)));
static uint32_t s_heartbeat_parameter __attribute__((aligned(32)));
static uint8_t s_control_sequence;
static uint64_t s_last_heartbeat;

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

status_t t5ai_cp_mailbox_send(const void *data, size_t length) {
  if (data == NULL || length == 0 || length > UINT32_MAX) {
    return E_INVALID_ARGUMENT;
  }
  const uint64_t start = prv_ticks();
  while ((T5AI_MBOX_CH0_FIFO_STATUS & T5AI_MBOX_FIFO_FULL) != 0) {
    if (prv_timed_out(start)) {
      return E_BUSY;
    }
  }
  T5AI_MBOX_CH1_TX_DATA0 = (uint32_t)(uintptr_t)data;
  T5AI_MBOX_CH1_TX_DATA1 = (uint32_t)length;
  T5AI_MBOX_CH1_TX_ID = T5AI_MBOX_CPU_CP;
  return S_SUCCESS;
}

static status_t prv_receive_physical(T5AICPMailboxFrame *frame,
                                     uint64_t start) {
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

static status_t prv_frame_channel(const T5AICPMailboxFrame *frame,
                                  uint8_t *logical_channel) {
  if (frame->address == 0 || frame->length < sizeof(uint32_t)) {
    return E_ERROR;
  }
  uint32_t header;
  dcache_invalidate((void *)frame->address, sizeof(header));
  memcpy(&header, (const void *)frame->address, sizeof(header));
  *logical_channel = (uint8_t)(header >> 24);
  return S_SUCCESS;
}

status_t t5ai_cp_mailbox_register(uint8_t logical_channel,
                                  T5AICPMailboxReceiveCallback callback,
                                  void *context) {
  if (callback == NULL) {
    return E_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
    if (s_routes[i].callback != NULL &&
        s_routes[i].logical_channel == logical_channel) {
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
    if (s_routes[i].callback != NULL &&
        s_routes[i].logical_channel == logical_channel) {
      memset(&s_routes[i], 0, sizeof(s_routes[i]));
      return;
    }
  }
}

status_t t5ai_cp_mailbox_receive(uint8_t logical_channel,
                                 T5AICPMailboxFrame *frame) {
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
    bool dispatched = false;
    for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
      if (s_routes[i].callback != NULL &&
          s_routes[i].logical_channel == received_channel) {
        s_routes[i].callback(received_channel, frame, s_routes[i].context);
        dispatched = true;
        break;
      }
    }
    if (!dispatched) {
      return E_DOES_NOT_EXIST;
    }
  }
}

status_t t5ai_cp_mailbox_try_receive(uint8_t logical_channel,
                                     T5AICPMailboxFrame *frame) {
  if (frame == NULL) {
    return E_INVALID_ARGUMENT;
  }
  if ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) == 0) {
    return E_AGAIN;
  }
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
  for (size_t i = 0; i < T5AI_MBOX_MAX_ROUTES; ++i) {
    if (s_routes[i].callback != NULL &&
        s_routes[i].logical_channel == received_channel) {
      s_routes[i].callback(received_channel, frame, s_routes[i].context);
      return E_AGAIN;
    }
  }
  return E_DOES_NOT_EXIST;
}

void t5ai_cp_mailbox_discard_pending(void) {
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) != 0) {
    (void)T5AI_MBOX_CH1_RX_ID;
    (void)T5AI_MBOX_CH1_RX_DATA0;
    (void)T5AI_MBOX_CH1_RX_DATA1;
  }
}

static void prv_send_control(uint8_t command, const void *buffer,
                             size_t length) {
  ++s_control_sequence;
  s_control_message.header = command |
                             ((uint32_t)s_control_sequence << 16) |
                             (T5AI_CP_CONTROL_CHANNEL << 24);
  s_control_message.buffer = (uint32_t)(uintptr_t)buffer;
  s_control_message.length = (uint32_t)length;
  s_control_message.reserved = 0u;
  if (buffer != NULL && length != 0u) {
    dcache_flush((void *)buffer, length);
  }
  dcache_flush(&s_control_message, sizeof(s_control_message));
  (void)t5ai_cp_mailbox_send(&s_control_message, sizeof(s_control_message));
}

void t5ai_cp_mailbox_announce_power_up(void) {
  prv_send_control(T5AI_CP_POWER_UP_COMMAND, NULL, 0u);
  s_last_heartbeat = prv_ticks();
}

void t5ai_cp_mailbox_heartbeat(void) {
  const uint64_t now = prv_ticks();
  if (now - s_last_heartbeat < T5AI_CP_HEARTBEAT_TICKS) {
    return;
  }
  s_last_heartbeat = now;
  s_heartbeat_parameter = 0u;
  prv_send_control(T5AI_CP_HEARTBEAT_COMMAND, &s_heartbeat_parameter,
                   sizeof(s_heartbeat_parameter));
}
