/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/cp_mailbox.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include <stdbool.h>
#include <stdint.h>

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

static SemaphoreHandle_t s_mutex;

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

status_t t5ai_cp_mailbox_receive(T5AICPMailboxFrame *frame) {
  if (frame == NULL) {
    return E_INVALID_ARGUMENT;
  }
  const uint64_t start = prv_ticks();
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

void t5ai_cp_mailbox_discard_pending(void) {
  while ((T5AI_MBOX_CH1_CTRL & T5AI_MBOX_FIFO_NOT_EMPTY) != 0) {
    (void)T5AI_MBOX_CH1_RX_ID;
    (void)T5AI_MBOX_CH1_RX_DATA0;
    (void)T5AI_MBOX_CH1_RX_DATA1;
  }
}
