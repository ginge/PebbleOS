/* SPDX-License-Identifier: Apache-2.0 */

#include "system/reboot_reason.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/kernel.h>
#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/rtc.h>

#include <cmsis_core.h>

// Physical mailbox header: command[7:0], status[11:8], control[15:12],
// sequence[23:16], logical channel[31:24]. Values are pinned vendor ABI.
#define MAILBOX_HEADER_COMMAND_MASK 0xffu
#define MAILBOX_HEADER_STATUS_MASK 0x00000f00u
#define MAILBOX_HEADER_LOGICAL_MASK 0x00000fffu
#define MAILBOX_HEADER_ROUTING_MASK 0xffff00ffu
#define MAILBOX_BUSY_RETRY_MS 1u
#define MAILBOX_MILLISECONDS_PER_SECOND 1000u

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
#define MBOX_SYNC_TX (2u << 12)
#define MBOX_RESET (4u << 12)
#define MBOX_CP_CPU_ID 0u

#define SYSTEM_CPU1_INT_32_63_ENABLE (*(volatile uint32_t *)0x4401008cu)
#define MAILBOX_SYSTEM_INTERRUPT_ENABLE (1u << 31)
#define MAILBOX_IRQ ((IRQn_Type)63)

/* Tuya's full AP mailbox worker reserves 2048 bytes. uint32_t is 32-bit on
 * T5AI. Keep the same capacity in an aligned, caller-owned SRAM stack. */
#define MAILBOX_TASK_STACK_SIZE       2048u
#define MAILBOX_TASK_PRIORITY (CONFIG_KERNEL_NUM_PRIORITIES - 1u)
#define MAILBOX_BOOT_POLL_MS 100u
#define MAILBOX_BOOTSTRAP_DRAIN_LIMIT 8u
#define MAILBOX_RUNTIME_DRAIN_LIMIT 8u
#define MAILBOX_ACTIVE_TIMEOUT_MS 5000u
#define MAILBOX_FAULT_POLL_MS 10u

typedef enum {
  TxState_Idle,
  TxState_Pending,
  TxState_Active,
  TxState_Complete,
} TxState;

typedef struct {
  T5AIMailboxMessage message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
  T5AIMailboxMessage ack;
  T5AIMailboxTransmitHandler handler;
  void *context;
  struct pbl_sem completion;
  struct pbl_mutex mutex;
  TxState state;
  bool synchronous;
  uint8_t sequence;
  uint32_t active_start;
} TxChannel;

typedef struct {
  T5AIMailboxReceiveHandler handler;
  void *context;
} RxChannel;

typedef struct {
  T5AIMailboxMessage message;
  uint8_t cache_line_padding[T5AI_SHARED_ALIGNMENT - sizeof(T5AIMailboxMessage)];
} __attribute__((aligned(T5AI_SHARED_ALIGNMENT))) MailboxOwnedSlot;

/* Command and ACK storage are on distinct cache lines. No cache clean ever
 * targets memory owned by the CP. */
static TxChannel s_tx[T5AI_MAILBOX_CHANNEL_COUNT];
static RxChannel s_rx[T5AI_MAILBOX_CHANNEL_COUNT];
/* The mailbox FIFO carries pointers, not message copies.  A CP producer does
 * not issue its next frame on a logical channel until it consumes the ACK for
 * the current frame, so a stable slot per receive channel gives the ACK an
 * exact protocol lifetime.  A small global rotating ring can be overwritten
 * by unrelated HCI and flash channels before the CP dereferences it. */
static MailboxOwnedSlot s_ack_slots[T5AI_MAILBOX_CHANNEL_COUNT];
static T5AIMailboxMessage s_reset_slot __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
static PBL_SEM_DEFINE(s_wake, 0, 1);
static struct pbl_thread s_task;
PBL_THREAD_STACK_DEFINE(s_task_stack, MAILBOX_TASK_STACK_SIZE);
static uint8_t s_sequence;
static uint8_t s_next_tx_channel;
static uint16_t s_pending_ack_mask;
static bool s_initialized;
static bool s_started;
static bool s_faulted;
static bool s_reset_sent;

_Static_assert(sizeof(T5AIMailboxMessage) == T5AI_MAILBOX_FRAME_SIZE, "T5AI mailbox ABI changed");

static void prv_send_reset(void);
static uint32_t prv_bootstrap_ticks(uint32_t milliseconds);

[[noreturn]] static void prv_reset_after_timeout(uint8_t channel) {
  t5ai_reboot_trace_event(T5RebootTrace_MailboxTimeout, channel);
  RebootReason reason = {
      .code = RebootReasonCode_MailboxTimeout,
      .extra.value = channel,
  };
  reboot_reason_set(&reason);
  t5ai_force_whole_chip_reset(T5AI_RESET_REASON_MAILBOX_BASE | channel);
}

static bool prv_any_active(void) {
  for (uint8_t index = 0u; index < T5AI_MAILBOX_CHANNEL_COUNT; ++index) {
    if (s_tx[index].state == TxState_Active) {
      return true;
    }
  }
  return false;
}

static int prv_expired_channel(void) {
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  for (uint8_t index = 0u; index < T5AI_MAILBOX_CHANNEL_COUNT; ++index) {
    TxChannel *tx = &s_tx[index];
    if (tx->state == TxState_Active &&
        (uint32_t)(now - tx->active_start) >= prv_bootstrap_ticks(MAILBOX_ACTIVE_TIMEOUT_MS)) {
      return index;
    }
  }
  return -1;
}

static uint32_t prv_milliseconds_to_ticks(uint32_t milliseconds) {
  return (milliseconds * PBL_TICK_HZ + MAILBOX_MILLISECONDS_PER_SECOND - 1u) /
         MAILBOX_MILLISECONDS_PER_SECOND;
}

static uint32_t prv_bootstrap_ticks(uint32_t milliseconds) {
  return (milliseconds * T5AI_AON_RTC_HZ + MAILBOX_MILLISECONDS_PER_SECOND - 1u) /
         MAILBOX_MILLISECONDS_PER_SECOND;
}

static bool prv_shared_range(uintptr_t address, size_t length) {
  if (length == 0u || address > UINTPTR_MAX - length) {
    return false;
  }
  const uintptr_t end = address + length;
  return (address >= T5AI_SHARED_SRAM_BEGIN && end <= T5AI_SHARED_SRAM_END) ||
         (address >= T5AI_SHARED_PSRAM_BEGIN && end <= T5AI_SHARED_PSRAM_END);
}

static bool prv_tx_channel(uint8_t channel, uint8_t *index) {
  if ((channel & T5AI_MAILBOX_CHANNEL_BASE_MASK) != T5AI_MAILBOX_TX_CHANNEL_BASE) {
    return false;
  }
  *index = channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK;
  return true;
}

static bool prv_rx_channel(uint8_t channel, uint8_t *index) {
  if ((channel & T5AI_MAILBOX_CHANNEL_BASE_MASK) != T5AI_MAILBOX_RX_CHANNEL_BASE) {
    return false;
  }
  *index = channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK;
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
  if (length != sizeof(*message) || !prv_shared_range(address, length)) {
    return false;
  }
  dcache_invalidate((void *)address, sizeof(*message));
  memcpy(message, (const void *)address, sizeof(*message));
  return true;
}

static void prv_finish_tx(TxChannel *tx, const T5AIMailboxMessage *ack, status_t status) {
  tx->ack = *ack;
  tx->state = tx->synchronous ? TxState_Complete : TxState_Idle;
  if (tx->synchronous) {
    (void)pbl_sem_give(&tx->completion);
  } else if (tx->handler != NULL) {
    T5AIMailboxTransmitHandler handler = tx->handler;
    void *context = tx->context;
    tx->handler = NULL;
    tx->context = NULL;
    handler(&tx->ack, status, context);
  }
}

static void prv_process_ack(T5AIMailboxMessage *ack) {
  const uint8_t channel = (uint8_t)(ack->header >> T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
  const uint8_t sequence = (uint8_t)(ack->header >> T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT);
  uint8_t index;
  if (!prv_tx_channel(channel, &index)) {
    return;
  }
  TxChannel *tx = &s_tx[index];
  if (tx->state != TxState_Active || sequence != tx->sequence) {
    return;
  }
  ack->header &= MAILBOX_HEADER_LOGICAL_MASK;
  const status_t status =
      (ack->header & T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED) == 0u ? S_SUCCESS : E_ERROR;
  prv_finish_tx(tx, ack, status);
}

static void prv_process_command(T5AIMailboxMessage *message) {
  const uint32_t physical_header = message->header;
  const uint8_t channel = (uint8_t)(physical_header >> T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
  uint8_t index = channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK;
  message->header = physical_header & MAILBOX_HEADER_COMMAND_MASK;
  if (prv_rx_channel(channel, &index) && s_rx[index].handler != NULL) {
    s_rx[index].handler(message, s_rx[index].context);
  } else {
    message->header |= T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED;
  }

  if ((physical_header & MBOX_SYNC_TX) != 0u) {
    return;
  }

  T5AIMailboxMessage *ack = &s_ack_slots[channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK].message;
  *ack = *message;
  ack->header = (physical_header & MAILBOX_HEADER_ROUTING_MASK) |
                (message->header & MAILBOX_HEADER_STATUS_MASK) | MBOX_ACK_BOX;
  /* ACKs bypass the ordinary transmit scheduler and are never ACKed. Keep
   * the vendor path's immediate attempt, but defer a busy ACK instead of
   * spinning at the maximum task priority. */
  if (prv_publish(ack) == E_BUSY) {
    s_pending_ack_mask |= (uint16_t)(1u << (channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK));
  }
}

static bool prv_drain_rx(void) {
  for (uint8_t drained = 0u;
       drained < MAILBOX_RUNTIME_DRAIN_LIMIT && (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u;
       ++drained) {
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
  return (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u;
}

static bool prv_publish_pending_acks(void) {
  for (uint8_t index = 0u; index < T5AI_MAILBOX_CHANNEL_COUNT; ++index) {
    const uint16_t pending_bit = (uint16_t)(1u << index);
    if ((s_pending_ack_mask & pending_bit) == 0u) {
      continue;
    }
    if (prv_publish(&s_ack_slots[index].message) == E_BUSY) {
      return false;
    }
    s_pending_ack_mask &= (uint16_t)~pending_bit;
  }
  return true;
}

static status_t prv_schedule_tx(void) {
  if (s_faulted) {
    return E_INVALID_OPERATION;
  }
  /* CPU0 shares two adapter buffers between commands and ACKs. Match the
   * vendor physical gate; logical channels keep independent pending slots. */
  if (prv_any_active()) {
    return S_SUCCESS;
  }
  for (uint8_t offset = 0; offset < T5AI_MAILBOX_CHANNEL_COUNT; ++offset) {
    const uint8_t index = (s_next_tx_channel + offset) % T5AI_MAILBOX_CHANNEL_COUNT;
    TxChannel *tx = &s_tx[index];
    if (tx->state != TxState_Pending) {
      continue;
    }
    ++s_sequence;
    tx->sequence = s_sequence;
    tx->message.header =
        (tx->message.header & MAILBOX_HEADER_COMMAND_MASK) |
        ((uint32_t)tx->sequence << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) |
        ((uint32_t)(T5AI_MAILBOX_TX_CHANNEL_BASE + index) << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
    if (prv_publish(&tx->message) == E_BUSY) {
      return E_BUSY;
    }
    tx->state = TxState_Active;
    tx->active_start = T5AI_AON_RTC_COUNT_LOW;
    s_next_tx_channel = (index + 1u) % T5AI_MAILBOX_CHANNEL_COUNT;
    return S_SUCCESS;
  }
  return S_SUCCESS;
}

static void prv_mailbox_task(void *context) {
  (void)context;
  while (true) {
    const pbl_tick_t heartbeat_wait = t5ai_cp_mailbox_heartbeat_poll();
    const pbl_tick_t fault_wait =
        !prv_any_active() ? PBL_TICK_FOREVER : prv_milliseconds_to_ticks(MAILBOX_FAULT_POLL_MS);
    const pbl_tick_t wait = heartbeat_wait < fault_wait ? heartbeat_wait : fault_wait;
    (void)pbl_sem_take(&s_wake, PBL_TICKS(wait));
    const bool rx_pending = prv_drain_rx();
    const int expired_channel = rx_pending ? -1 : prv_expired_channel();
    if (expired_channel >= 0) {
      s_faulted = true;
      prv_reset_after_timeout((uint8_t)expired_channel);
    }
    const bool ack_pending = !prv_publish_pending_acks();
    /* Retire queued RX frames before ordinary TX can make CPU0 reuse an
     * adapter slot. Physical ACKs bypass the single-command gate. */
    bool reset_pending = false;
    if (!s_reset_sent) {
      prv_send_reset();
      reset_pending = !s_reset_sent;
    }
    const status_t schedule_status =
        (rx_pending || ack_pending || reset_pending) ? E_BUSY : prv_schedule_tx();
    MBOX_CH1_CTRL |= MBOX_INTERRUPT_ENABLE;
    if (rx_pending || ack_pending || reset_pending || schedule_status == E_BUSY) {
      /* Blocking for a tick is essential here. This maximum-priority task must
       * not starve the tasks whose timeouts make mailbox failures recoverable,
       * even if CP traffic never leaves the receive FIFO empty. */
      pbl_thread_sleep(PBL_MSEC(MAILBOX_BUSY_RETRY_MS));
      (void)pbl_sem_give(&s_wake);
      continue;
    }
    /* Close the IRQ-mask race if a frame arrived between drain and enable. */
    if ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u) {
      MBOX_CH1_CTRL &= ~MBOX_INTERRUPT_ENABLE;
      (void)pbl_sem_give(&s_wake);
    }
  }
}

void MAILBOX_IRQHandler(void) {
  const uint32_t irq_start = t5ai_interrupt_profile_irq_enter();
  MBOX_CH1_CTRL &= ~MBOX_INTERRUPT_ENABLE;
  bool task_woken = false;
  if (s_initialized) {
    (void)pbl_sem_give(&s_wake);
  }
  (void)(task_woken);
  t5ai_interrupt_profile_irq_exit(2, irq_start);
}

static void prv_send_reset(void) {
  if (s_reset_sent) {
    return;
  }
  s_reset_slot = (T5AIMailboxMessage){
      .header = ((uint32_t)T5AI_MAILBOX_RX_CHANNEL_BASE << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT) |
                MBOX_ACK_BOX | MBOX_RESET,
  };
  if (prv_publish(&s_reset_slot) == S_SUCCESS) {
    s_reset_sent = true;
  }
}

static bool prv_bootstrap_publish(const T5AIMailboxMessage *message, uint32_t timeout_ms) {
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t timeout_ticks = prv_bootstrap_ticks(timeout_ms);
  do {
    if (prv_publish(message) == S_SUCCESS) {
      return true;
    }
  } while ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < timeout_ticks);
  return false;
}

static void prv_bootstrap_resynchronize(uint32_t timeout_ms) {
  /* CP0 can publish before CPU1 reaches its scheduler, and it survives an
   * AP-only reset. Reject unhandled frames from an AP-owned cache line before
   * opening the power-up transaction. Never clean or republish the CP pointer.
   *
   * CP0 remains live while this runs and may publish new work as slots are
   * retired. This is therefore a bounded recovery pass, not a drain-until-idle
   * loop: startup must always progress to the mandatory power-up indication. */
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t timeout_ticks = prv_bootstrap_ticks(timeout_ms);
  for (uint32_t drained = 0u; drained < MAILBOX_BOOTSTRAP_DRAIN_LIMIT &&
                              (uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < timeout_ticks &&
                              (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u;
       ++drained) {
    T5AIMailboxMessage message;
    if (!prv_snapshot_rx(&message) || (message.header & MBOX_ACK_BOX) != 0u) {
      continue;
    }
    prv_process_command(&message);
    while (!prv_publish_pending_acks() &&
           (uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < timeout_ticks) {
    }
    if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) >= timeout_ticks) {
      break;
    }
  }
}

status_t t5ai_mailbox_channel_init(void) {
  if (s_initialized) {
    return S_SUCCESS;
  }
  for (unsigned i = 0; i < T5AI_MAILBOX_CHANNEL_COUNT; ++i) {
    pbl_sem_init(&s_tx[i].completion, 0, 1);
    pbl_mutex_init(&s_tx[i].mutex);
  }
  /* The mailbox must run before the CP powers PSRAM for the compositor. Keep
   * this bootstrap task's stack in internal SRAM; touching a board-cache stack
   * here would stall while the native kernel builds the initial stack frame. */
  const struct pbl_thread_attr attr = {
    .name = "T5AI mailbox",
    .entry = prv_mailbox_task,
    .prio = MAILBOX_TASK_PRIORITY,
    .privileged = true,
    .stack = s_task_stack,
    .stack_size = sizeof(s_task_stack),
  };
  if (pbl_thread_create(&s_task, &attr) != 0) {
    return E_OUT_OF_MEMORY;
  }
  s_initialized = true;
  return S_SUCCESS;
}

void t5ai_mailbox_channel_wake(void) {
  (void)pbl_sem_give(&s_wake);
}

status_t t5ai_mailbox_channel_start(void) {
  status_t status = t5ai_mailbox_channel_init();
  if (FAILED(status)) {
    return status;
  }
  if (s_started) {
    return s_faulted ? E_INVALID_OPERATION : S_SUCCESS;
  }
  s_started = true;
  SYSTEM_CPU1_INT_32_63_ENABLE |= MAILBOX_SYSTEM_INTERRUPT_ENABLE;
  NVIC_SetPriority(MAILBOX_IRQ, (PBL_IRQ_PRIO_MAX_SYSCALL >> (8u - __NVIC_PRIO_BITS)));
  NVIC_EnableIRQ(MAILBOX_IRQ);
  MBOX_CH1_CTRL |= MBOX_INTERRUPT_ENABLE;
  (void)pbl_sem_give(&s_wake);
  return S_SUCCESS;
}

status_t t5ai_mailbox_channel_register(uint8_t channel, T5AIMailboxReceiveHandler handler,
                                       void *context) {
  uint8_t index;
  if (!prv_rx_channel(channel, &index) || handler == NULL) {
    return E_INVALID_ARGUMENT;
  }
  pbl_irq_lock();
  if (s_rx[index].handler != NULL) {
    pbl_irq_unlock();
    return E_INVALID_OPERATION;
  }
  s_rx[index] = (RxChannel){.handler = handler, .context = context};
  pbl_irq_unlock();
  return S_SUCCESS;
}

void t5ai_mailbox_channel_unregister(uint8_t channel) {
  uint8_t index;
  if (!prv_rx_channel(channel, &index)) {
    return;
  }
  pbl_irq_lock();
  s_rx[index] = (RxChannel){0};
  pbl_irq_unlock();
}

status_t t5ai_mailbox_channel_send_async(uint8_t channel, const T5AIMailboxMessage *message,
                                         T5AIMailboxTransmitHandler handler, void *context) {
  uint8_t index;
  if (!s_started || s_faulted || !prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  TxChannel *tx = &s_tx[index];
  pbl_irq_lock();
  if (tx->state != TxState_Idle) {
    pbl_irq_unlock();
    return E_BUSY;
  }
  tx->message = *message;
  tx->handler = handler;
  tx->context = context;
  tx->synchronous = false;
  tx->state = TxState_Pending;
  pbl_irq_unlock();
  (void)pbl_sem_give(&s_wake);
  return S_SUCCESS;
}

status_t t5ai_mailbox_channel_send(uint8_t channel, const T5AIMailboxMessage *message,
                                   T5AIMailboxMessage *ack, uint32_t timeout_ms) {
  uint8_t index;
  if (!s_started || s_faulted || !prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  TxChannel *tx = &s_tx[index];
  pbl_mutex_lock(&tx->mutex, PBL_FOREVER);
  while (pbl_sem_take(&tx->completion, PBL_TICKS(0u)) == 0) {
  }
  pbl_irq_lock();
  if (tx->state != TxState_Idle) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&tx->mutex);
    return E_BUSY;
  }
  tx->message = *message;
  tx->synchronous = true;
  tx->handler = NULL;
  tx->context = NULL;
  tx->state = TxState_Pending;
  pbl_irq_unlock();
  (void)pbl_sem_give(&s_wake);

  const bool taken = pbl_sem_take(
      &tx->completion, PBL_TICKS(timeout_ms == UINT32_MAX ? PBL_TICK_FOREVER
                                        : prv_milliseconds_to_ticks(timeout_ms)));
  if (taken != 0) {
    /* Returning would let the caller reuse memory which may still be visible
     * through this descriptor. Recovery must reset both CPUs and the mailbox
     * before any transaction storage is recycled. */
    s_faulted = true;
    prv_reset_after_timeout(index);
  }
  if (ack != NULL) {
    *ack = tx->ack;
  }
  const status_t status =
      (tx->ack.header & T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED) == 0u ? S_SUCCESS : E_ERROR;
  pbl_irq_lock();
  tx->state = TxState_Idle;
  pbl_irq_unlock();
  pbl_mutex_unlock(&tx->mutex);
  return status;
}

bool t5ai_mailbox_channel_faulted(void) {
  return s_faulted;
}

status_t t5ai_mailbox_channel_bootstrap_send(uint8_t channel, const T5AIMailboxMessage *message,
                                             T5AIMailboxMessage *ack, uint32_t timeout_ms) {
  uint8_t index;
  if (!prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  static T5AIMailboxMessage boot_message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
  boot_message = *message;
  ++s_sequence;
  boot_message.header = (boot_message.header & MAILBOX_HEADER_COMMAND_MASK) |
                        ((uint32_t)s_sequence << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) |
                        ((uint32_t)channel << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
  const uint32_t poll_ms = timeout_ms == 0u ? MAILBOX_BOOT_POLL_MS : timeout_ms;
  prv_bootstrap_resynchronize(poll_ms);
  if (!prv_bootstrap_publish(&boot_message, poll_ms)) {
    return E_BUSY;
  }
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t timeout_ticks = prv_bootstrap_ticks(poll_ms);
  do {
    if ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u) {
      T5AIMailboxMessage response;
      if (!prv_snapshot_rx(&response)) {
        continue;
      }
      if ((response.header & MBOX_ACK_BOX) == 0u) {
        prv_process_command(&response);
        while (!prv_publish_pending_acks() &&
               (uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < timeout_ticks) {
        }
        continue;
      }
      if ((response.header & MBOX_ACK_BOX) != 0u &&
          (uint8_t)(response.header >> T5AI_MAILBOX_HEADER_CHANNEL_SHIFT) == channel &&
          (uint8_t)(response.header >> T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) == s_sequence) {
        response.header &= MAILBOX_HEADER_LOGICAL_MASK;
        if (ack != NULL) {
          *ack = response;
        }
        return (response.header & T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED) == 0u ? S_SUCCESS
                                                                                  : E_ERROR;
      }
    }
  } while ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) < timeout_ticks);
  return E_AGAIN;
}

PBL_IRQ_CONNECT(MAILBOX, 5, MAILBOX_IRQHandler, , 0);
