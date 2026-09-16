/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/mcu/cache.h>
#include <pbl/os/mutex.h>
#include <pbl/soc/t5ai/mailbox_channel.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <cmsis_core.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MBOX_BASE 0x41000000u
#define MBOX_CH1_CTRL (*(volatile uint32_t *)(MBOX_BASE + 0x80u))
#define MBOX_CH1_TX_DATA0 (*(volatile uint32_t *)(MBOX_BASE + 0x88u))
#define MBOX_CH1_TX_DATA1 (*(volatile uint32_t *)(MBOX_BASE + 0x8cu))
#define MBOX_CH1_TX_ID (*(volatile uint32_t *)(MBOX_BASE + 0x90u))
#define MBOX_CH1_RX_ID (*(volatile uint32_t *)(MBOX_BASE + 0x94u))
#define MBOX_CH1_RX_DATA0 (*(volatile uint32_t *)(MBOX_BASE + 0x98u))
#define MBOX_CH1_RX_DATA1 (*(volatile uint32_t *)(MBOX_BASE + 0x9cu))
#define MBOX_CH0_FIFO_STATUS (*(volatile uint32_t *)(MBOX_BASE + 0x60u))

#define MBOX_FIFO_FULL (1u << 0)
#define MBOX_FIFO_NOT_EMPTY (1u << 19)
#define MBOX_INTERRUPT_ENABLE (1u << 8)
#define MBOX_ACK_BOX (1u << 12)
#define MBOX_RESET (4u << 12)
#define MBOX_COMMUNICATION_FAILED (1u << 8)
#define MBOX_CP_CPU_ID 0u

#define SYSTEM_CPU1_INT_32_63_ENABLE (*(volatile uint32_t *)0x4401008cu)
#define MAILBOX_SYSTEM_INTERRUPT_ENABLE (1u << 31)
#define MAILBOX_IRQ ((IRQn_Type)63)

/* Tuya's full AP mailbox worker reserves 2048 bytes. StackType_t is 32-bit on
 * T5AI, so 512 words preserves the vendor-qualified capacity without wasting
 * a further 1 KiB of the scarce internal kernel heap. */
#define MAILBOX_TASK_STACK_WORDS 512u
#define MAILBOX_TASK_PRIORITY (configMAX_PRIORITIES - 1u)
#define MAILBOX_BOOT_POLL_MS 100u
#define MAILBOX_BOOTSTRAP_DRAIN_LIMIT 8u
#define AON_RTC_LOW (*(volatile uint32_t *)0x4400020cu)
#define AON_RTC_HZ 32000u

typedef enum {
  TxState_Idle,
  TxState_Pending,
  TxState_Active,
} TxState;

typedef struct {
  T5AIMailboxMessage message __attribute__((aligned(32)));
  T5AIMailboxMessage ack;
  T5AIMailboxTransmitHandler handler;
  void *context;
  SemaphoreHandle_t completion;
  PebbleMutex *mutex;
  TxState state;
  bool synchronous;
} TxChannel;

typedef struct {
  T5AIMailboxReceiveHandler handler;
  void *context;
} RxChannel;

typedef struct {
  T5AIMailboxMessage message;
  uint8_t cache_line_padding[16];
} __attribute__((aligned(32))) MailboxOwnedSlot;

/* Command and ACK storage are on distinct cache lines. No cache clean ever
 * targets memory owned by the CP. */
static TxChannel s_tx[T5AI_MAILBOX_CHANNEL_COUNT];
static RxChannel s_rx[T5AI_MAILBOX_CHANNEL_COUNT];
static MailboxOwnedSlot s_ack_slots[2];
static T5AIMailboxMessage s_reset_slot __attribute__((aligned(32)));
static SemaphoreHandle_t s_wake;
static TaskHandle_t s_task;
static uint8_t s_ack_slot;
static uint8_t s_sequence;
static int8_t s_active_channel = -1;
static bool s_initialized;
static bool s_reset_sent;

_Static_assert(sizeof(T5AIMailboxMessage) == T5AI_MAILBOX_FRAME_SIZE,
               "T5AI mailbox ABI changed");

static uint32_t prv_milliseconds_to_ticks(uint32_t milliseconds) {
  return (milliseconds * configTICK_RATE_HZ + 999u) / 1000u;
}

static uint32_t prv_bootstrap_ticks(uint32_t milliseconds) {
  return (milliseconds * AON_RTC_HZ + 999u) / 1000u;
}

static bool prv_tx_channel(uint8_t channel, uint8_t *index) {
  if ((channel & 0xf0u) != T5AI_MAILBOX_TX_CHANNEL_BASE) {
    return false;
  }
  *index = channel & 0x0fu;
  return true;
}

static bool prv_rx_channel(uint8_t channel, uint8_t *index) {
  if ((channel & 0xf0u) != T5AI_MAILBOX_RX_CHANNEL_BASE) {
    return false;
  }
  *index = channel & 0x0fu;
  return true;
}

static status_t prv_publish(const T5AIMailboxMessage *message) {
  if ((MBOX_CH0_FIFO_STATUS & MBOX_FIFO_FULL) != 0u) {
    return E_BUSY;
  }
  dcache_flush((void *)message, sizeof(*message));
  MBOX_CH1_TX_DATA0 = (uint32_t)(uintptr_t)message;
  MBOX_CH1_TX_DATA1 = sizeof(*message);
  __DSB();
  MBOX_CH1_TX_ID = MBOX_CP_CPU_ID;
  __DSB();
  return S_SUCCESS;
}

static bool prv_snapshot_rx(T5AIMailboxMessage *message) {
  (void)MBOX_CH1_RX_ID;
  const uintptr_t address = MBOX_CH1_RX_DATA0;
  const uint32_t length = MBOX_CH1_RX_DATA1;
  if (address == 0u || length != sizeof(*message)) {
    return false;
  }
  dcache_invalidate((void *)address, sizeof(*message));
  memcpy(message, (const void *)address, sizeof(*message));
  return true;
}

static void prv_finish_tx(TxChannel *tx, const T5AIMailboxMessage *ack, status_t status) {
  tx->ack = *ack;
  tx->state = TxState_Idle;
  if (tx->synchronous) {
    (void)xSemaphoreGive(tx->completion);
  } else if (tx->handler != NULL) {
    T5AIMailboxTransmitHandler handler = tx->handler;
    void *context = tx->context;
    tx->handler = NULL;
    tx->context = NULL;
    handler(&tx->ack, status, context);
  }
}

static void prv_process_ack(T5AIMailboxMessage *ack) {
  const uint8_t channel = (uint8_t)(ack->header >> 24u);
  const uint8_t sequence = (uint8_t)(ack->header >> 16u);
  uint8_t index;
  if (!prv_tx_channel(channel, &index) || s_active_channel != (int8_t)index) {
    return;
  }
  TxChannel *tx = &s_tx[index];
  if (tx->state != TxState_Active || sequence != s_sequence) {
    return;
  }
  ack->header &= 0x00000fffu;
  s_active_channel = -1;
  const status_t status = (ack->header & MBOX_COMMUNICATION_FAILED) == 0u ? S_SUCCESS : E_ERROR;
  prv_finish_tx(tx, ack, status);
}

static void prv_process_command(T5AIMailboxMessage *message) {
  const uint32_t physical_header = message->header;
  const uint8_t channel = (uint8_t)(physical_header >> 24u);
  uint8_t index;
  message->header = physical_header & 0xffu;
  if (prv_rx_channel(channel, &index) && s_rx[index].handler != NULL) {
    s_rx[index].handler(message, s_rx[index].context);
  } else {
    message->header |= MBOX_COMMUNICATION_FAILED;
  }

  T5AIMailboxMessage *ack = &s_ack_slots[s_ack_slot].message;
  s_ack_slot ^= 1u;
  *ack = *message;
  ack->header = (physical_header & 0xffff00ffu) | (message->header & 0x00000f00u) | MBOX_ACK_BOX;
  /* ACKs bypass the ordinary single-frame scheduler and are never ACKed. */
  while (prv_publish(ack) == E_BUSY) {
    taskYIELD();
  }
}

static void prv_drain_rx(void) {
  while ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u) {
    T5AIMailboxMessage message;
    if (!prv_snapshot_rx(&message)) {
      continue;
    }
    if ((message.header & MBOX_ACK_BOX) != 0u) {
      prv_process_ack(&message);
    } else {
      prv_process_command(&message);
    }
  }
}

static status_t prv_schedule_tx(void) {
  if (s_active_channel >= 0) {
    return S_SUCCESS;
  }
  for (uint8_t index = 0; index < T5AI_MAILBOX_CHANNEL_COUNT; ++index) {
    TxChannel *tx = &s_tx[index];
    if (tx->state != TxState_Pending) {
      continue;
    }
    ++s_sequence;
    tx->message.header = (tx->message.header & 0xffu) |
                         ((uint32_t)s_sequence << 16u) |
                         ((uint32_t)(T5AI_MAILBOX_TX_CHANNEL_BASE + index) << 24u);
    if (prv_publish(&tx->message) == E_BUSY) {
      return E_BUSY;
    }
    tx->state = TxState_Active;
    s_active_channel = (int8_t)index;
    return S_SUCCESS;
  }
  return S_SUCCESS;
}

static status_t prv_ensure_sync_channel(TxChannel *tx) {
  if (tx->completion == NULL) {
    tx->completion = xSemaphoreCreateBinary();
  }
  if (tx->mutex == NULL) {
    tx->mutex = mutex_create();
  }
  return tx->completion != NULL && tx->mutex != NULL ? S_SUCCESS : E_OUT_OF_MEMORY;
}

static void prv_mailbox_task(void *context) {
  (void)context;
  while (true) {
    (void)xSemaphoreTake(s_wake, portMAX_DELAY);
    prv_drain_rx();
    const status_t schedule_status = prv_schedule_tx();
    MBOX_CH1_CTRL |= MBOX_INTERRUPT_ENABLE;
    /* Close the IRQ-mask race if a frame arrived between drain and enable. */
    if ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u) {
      MBOX_CH1_CTRL &= ~MBOX_INTERRUPT_ENABLE;
      (void)xSemaphoreGive(s_wake);
    } else if (schedule_status == E_BUSY) {
      vTaskDelay(pdMS_TO_TICKS(1u));
      (void)xSemaphoreGive(s_wake);
    }
  }
}

void MAILBOX_IRQHandler(void) {
  MBOX_CH1_CTRL &= ~MBOX_INTERRUPT_ENABLE;
  BaseType_t task_woken = pdFALSE;
  if (s_wake != NULL) {
    (void)xSemaphoreGiveFromISR(s_wake, &task_woken);
  }
  portEND_SWITCHING_ISR(task_woken);
}

static void prv_send_reset(void) {
  if (s_reset_sent) {
    return;
  }
  s_reset_slot = (T5AIMailboxMessage){
      .header = ((uint32_t)T5AI_MAILBOX_RX_CHANNEL_BASE << 24u) | MBOX_ACK_BOX | MBOX_RESET,
  };
  if (prv_publish(&s_reset_slot) == S_SUCCESS) {
    s_reset_sent = true;
  }
}

static bool prv_bootstrap_publish(const T5AIMailboxMessage *message, uint32_t timeout_ms) {
  const uint32_t start = AON_RTC_LOW;
  const uint32_t timeout_ticks = prv_bootstrap_ticks(timeout_ms);
  do {
    if (prv_publish(message) == S_SUCCESS) {
      return true;
    }
  } while ((uint32_t)(AON_RTC_LOW - start) < timeout_ticks);
  return false;
}

static void prv_bootstrap_resynchronize(uint32_t timeout_ms) {
  /* CP0 can publish before CPU1 reaches its scheduler, and it survives an
   * AP-only reset. Retire every such frame from an AP-owned cache line before
   * opening the power-up transaction. Never clean or republish the CP pointer.
   *
   * CP0 remains live while this runs and may publish new work as slots are
   * retired. This is therefore a bounded recovery pass, not a drain-until-idle
   * loop: startup must always progress to the mandatory power-up indication. */
  const uint32_t start = AON_RTC_LOW;
  const uint32_t timeout_ticks = prv_bootstrap_ticks(timeout_ms);
  for (uint32_t drained = 0u;
       drained < MAILBOX_BOOTSTRAP_DRAIN_LIMIT &&
       (uint32_t)(AON_RTC_LOW - start) < timeout_ticks &&
       (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u;
       ++drained) {
    T5AIMailboxMessage message;
    if (!prv_snapshot_rx(&message) || (message.header & MBOX_ACK_BOX) != 0u) {
      continue;
    }
    T5AIMailboxMessage *ack = &s_ack_slots[s_ack_slot].message;
    s_ack_slot ^= 1u;
    *ack = message;
    ack->header = (ack->header & ~(0x0fu << 8u)) | MBOX_ACK_BOX;
    do {
      if (prv_publish(ack) == S_SUCCESS) {
        break;
      }
    } while ((uint32_t)(AON_RTC_LOW - start) < timeout_ticks);
    if ((uint32_t)(AON_RTC_LOW - start) >= timeout_ticks) {
      break;
    }
  }
}

status_t t5ai_mailbox_channel_init(void) {
  if (s_initialized) {
    return S_SUCCESS;
  }
  s_wake = xSemaphoreCreateBinary();
  if (s_wake == NULL) {
    return E_OUT_OF_MEMORY;
  }
  /* The mailbox must run before the CP powers PSRAM for the compositor. Keep
   * this bootstrap task's stack in internal SRAM; touching a board-cache stack
   * here would stall while FreeRTOS builds the initial stack frame. */
  if (xTaskCreate(prv_mailbox_task, "T5AI mailbox", MAILBOX_TASK_STACK_WORDS, NULL,
                  MAILBOX_TASK_PRIORITY, &s_task) != pdPASS) {
    return E_OUT_OF_MEMORY;
  }
  s_initialized = true;
  SYSTEM_CPU1_INT_32_63_ENABLE |= MAILBOX_SYSTEM_INTERRUPT_ENABLE;
  NVIC_SetPriority(MAILBOX_IRQ, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_EnableIRQ(MAILBOX_IRQ);
  MBOX_CH1_CTRL |= MBOX_INTERRUPT_ENABLE;
  prv_send_reset();
  (void)xSemaphoreGive(s_wake);
  return S_SUCCESS;
}

status_t t5ai_mailbox_channel_register(uint8_t channel, T5AIMailboxReceiveHandler handler,
                                       void *context) {
  uint8_t index;
  if (!prv_rx_channel(channel, &index) || handler == NULL) {
    return E_INVALID_ARGUMENT;
  }
  taskENTER_CRITICAL();
  if (s_rx[index].handler != NULL) {
    taskEXIT_CRITICAL();
    return E_INVALID_OPERATION;
  }
  s_rx[index] = (RxChannel){.handler = handler, .context = context};
  taskEXIT_CRITICAL();
  return S_SUCCESS;
}

void t5ai_mailbox_channel_unregister(uint8_t channel) {
  uint8_t index;
  if (!prv_rx_channel(channel, &index)) {
    return;
  }
  taskENTER_CRITICAL();
  s_rx[index] = (RxChannel){0};
  taskEXIT_CRITICAL();
}

status_t t5ai_mailbox_channel_send_async(uint8_t channel, const T5AIMailboxMessage *message,
                                         T5AIMailboxTransmitHandler handler, void *context) {
  uint8_t index;
  if (!s_initialized || !prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  TxChannel *tx = &s_tx[index];
  taskENTER_CRITICAL();
  if (tx->state != TxState_Idle) {
    taskEXIT_CRITICAL();
    return E_BUSY;
  }
  tx->message = *message;
  tx->handler = handler;
  tx->context = context;
  tx->synchronous = false;
  tx->state = TxState_Pending;
  taskEXIT_CRITICAL();
  (void)xSemaphoreGive(s_wake);
  return S_SUCCESS;
}

status_t t5ai_mailbox_channel_send(uint8_t channel, const T5AIMailboxMessage *message,
                                   T5AIMailboxMessage *ack, uint32_t timeout_ms) {
  uint8_t index;
  if (!s_initialized || !prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  TxChannel *tx = &s_tx[index];
  status_t init_status = prv_ensure_sync_channel(tx);
  if (FAILED(init_status)) {
    return init_status;
  }
  mutex_lock(tx->mutex);
  while (xSemaphoreTake(tx->completion, 0u) == pdPASS) {
  }
  taskENTER_CRITICAL();
  if (tx->state != TxState_Idle) {
    taskEXIT_CRITICAL();
    mutex_unlock(tx->mutex);
    return E_BUSY;
  }
  tx->message = *message;
  tx->synchronous = true;
  tx->handler = NULL;
  tx->context = NULL;
  tx->state = TxState_Pending;
  taskEXIT_CRITICAL();
  (void)xSemaphoreGive(s_wake);

  const BaseType_t taken =
      xSemaphoreTake(tx->completion, timeout_ms == UINT32_MAX
                                        ? portMAX_DELAY
                                        : prv_milliseconds_to_ticks(timeout_ms));
  if (taken != pdPASS) {
    /* Generation quarantine is handled by the logical client. Do not recycle
     * a descriptor that may still be visible to the CP. */
    mutex_unlock(tx->mutex);
    return E_AGAIN;
  }
  if (ack != NULL) {
    *ack = tx->ack;
  }
  const status_t status = (tx->ack.header & MBOX_COMMUNICATION_FAILED) == 0u ? S_SUCCESS : E_ERROR;
  mutex_unlock(tx->mutex);
  return status;
}

status_t t5ai_mailbox_channel_bootstrap_send(uint8_t channel,
                                             const T5AIMailboxMessage *message,
                                             T5AIMailboxMessage *ack, uint32_t timeout_ms) {
  uint8_t index;
  if (!prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  static T5AIMailboxMessage boot_message __attribute__((aligned(32)));
  boot_message = *message;
  ++s_sequence;
  boot_message.header = (boot_message.header & 0xffu) | ((uint32_t)s_sequence << 16u) |
                        ((uint32_t)channel << 24u);
  const uint32_t poll_ms = timeout_ms == 0u ? MAILBOX_BOOT_POLL_MS : timeout_ms;
  prv_bootstrap_resynchronize(poll_ms);
  if (!prv_bootstrap_publish(&boot_message, poll_ms)) {
    return E_BUSY;
  }
  const uint32_t start = AON_RTC_LOW;
  const uint32_t timeout_ticks = prv_bootstrap_ticks(poll_ms);
  do {
    if ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u) {
      T5AIMailboxMessage response;
      if (prv_snapshot_rx(&response) && (response.header & MBOX_ACK_BOX) != 0u &&
          (uint8_t)(response.header >> 24u) == channel &&
          (uint8_t)(response.header >> 16u) == s_sequence) {
        response.header &= 0x00000fffu;
        if (ack != NULL) {
          *ack = response;
        }
        return (response.header & MBOX_COMMUNICATION_FAILED) == 0u ? S_SUCCESS : E_ERROR;
      }
    }
  } while ((uint32_t)(AON_RTC_LOW - start) < timeout_ticks);
  return E_AGAIN;
}
