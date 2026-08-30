/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/mcu/cache.h>

#include "FreeRTOS.h"
#include "semphr.h"

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

typedef struct {
  uint8_t logical_channel;
  T5AICPMailboxReceiveCallback callback;
  void *context;
} T5AICPMailboxRoute;

static SemaphoreHandle_t s_mutex;
static T5AICPMailboxRoute s_routes[T5AI_MBOX_MAX_ROUTES];

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
    s_mutex = xSemaphoreCreateMutex();
  }
  SemaphoreHandle_t mutex = s_mutex;
  if (mutex == NULL) {
    return E_OUT_OF_MEMORY;
  }
  return xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE ? S_SUCCESS : E_BUSY;
}

void t5ai_cp_mailbox_unlock(void) {
  if (s_mutex != NULL) {
    (void)xSemaphoreGive(s_mutex);
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

void t5ai_cp_mailbox_discard_pending(void) {
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) != 0) {
    (void)T5AI_MBOX_CH1_RX_ID;
    (void)T5AI_MBOX_CH1_RX_DATA0;
    (void)T5AI_MBOX_CH1_RX_DATA1;
  }
}
