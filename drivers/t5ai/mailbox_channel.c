/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/reboot_reason.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pbl/kernel/kernel.h>
#include <pbl/mcu/interrupts.h>
#include <pbl/soc/t5ai/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/rtc.h>
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
#include <pbl/soc/t5ai/sleep.h>
#endif

#include <pbl/soc/t5ai/core.h>

#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/fault_trace.h>
#include <pbl/soc/t5ai/sleep_protocol.h>
PBL_LOG_MODULE_DEFINE(t5ai_mailbox_sleep_test, LOG_LEVEL_DEBUG);
#endif

// Physical mailbox header: command[7:0], status[11:8], control[15:12],
// sequence[23:16], logical channel[31:24]. Values are pinned vendor ABI.
#define MAILBOX_HEADER_COMMAND_MASK     0xffu
#define MAILBOX_HEADER_STATUS_MASK      0x00000f00u
#define MAILBOX_HEADER_LOGICAL_MASK     0x00000fffu
#define MAILBOX_HEADER_ROUTING_MASK     0xffff00ffu
#define MAILBOX_BUSY_RETRY_MS           1u
#define MAILBOX_MILLISECONDS_PER_SECOND 1000u

#define MBOX_BASE            0x41000000u
#define MBOX_CH0_CTRL        (*(volatile uint32_t *)(MBOX_BASE + 0x40u))
#define MBOX_CH1_CTRL        (*(volatile uint32_t *)(MBOX_BASE + 0x80u))
#define MBOX_CH1_TX_DATA0    (*(volatile uint32_t *)(MBOX_BASE + 0x88u))
#define MBOX_CH1_TX_DATA1    (*(volatile uint32_t *)(MBOX_BASE + 0x8cu))
#define MBOX_CH1_TX_ID       (*(volatile uint32_t *)(MBOX_BASE + 0x90u))
#define MBOX_CH1_RX_ID       (*(volatile uint32_t *)(MBOX_BASE + 0x94u))
#define MBOX_CH1_RX_DATA0    (*(volatile uint32_t *)(MBOX_BASE + 0x98u))
#define MBOX_CH1_RX_DATA1    (*(volatile uint32_t *)(MBOX_BASE + 0x9cu))
#define MBOX_CH0_FIFO_STATUS (*(volatile uint32_t *)(MBOX_BASE + 0x60u))

#define MBOX_FIFO_FULL        (1u << 0)
#define MBOX_FIFO_NOT_EMPTY   (1u << 19)
#define MBOX_INTERRUPT_ENABLE (1u << 8)
#define MBOX_ACK_BOX          (1u << 12)
#define MBOX_SYNC_TX          (2u << 12)
#define MBOX_RESET            (4u << 12)
#define MBOX_CP_CPU_ID        0u

#define SYSTEM_CPU1_INT_32_63_ENABLE    (*(volatile uint32_t *)0x4401008cu)
#define MAILBOX_SYSTEM_INTERRUPT_ENABLE (1u << 31)
#define MAILBOX_IRQ                     ((IRQn_Type)63)

/* Tuya's full AP mailbox worker reserves 2048 bytes. uint32_t is 32-bit on
 * T5AI. Keep the same capacity in an aligned, caller-owned SRAM stack. */
#define MAILBOX_TASK_STACK_SIZE       2048u
#define MAILBOX_TASK_PRIORITY         (CONFIG_KERNEL_NUM_PRIORITIES - 1u)
#define MAILBOX_BOOT_POLL_MS          100u
#define MAILBOX_BOOTSTRAP_DRAIN_LIMIT 8u
#define MAILBOX_RUNTIME_DRAIN_LIMIT   8u
#define MAILBOX_ACTIVE_TIMEOUT_MS     5000u
#define MAILBOX_FAULT_POLL_MS         10u

typedef enum {
  TxState_Idle,
  TxState_Pending,
  TxState_Active,
  TxState_Complete,
} TxState;

typedef struct {
  T5AIMailboxMessage message __attribute__((aligned(T5AI_SHARED_ALIGNMENT)));
  uint8_t message_padding[T5AI_SHARED_ALIGNMENT - sizeof(T5AIMailboxMessage)];
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

/* CP reuses command storage for ACKs. Keep AP state outside its cache line. */
static TxChannel s_tx[T5AI_MAILBOX_CHANNEL_COUNT];
static RxChannel s_rx[T5AI_MAILBOX_CHANNEL_COUNT];
/* The mailbox FIFO carries pointers, not message copies.  A CP producer does
 * not issue its next frame on a logical channel until it consumes the ACK for
 * the current frame, so a stable slot per receive channel gives the ACK an
 * exact protocol lifetime.  A small global rotating ring can be overwritten
 * by unrelated HCI and flash channels before the CP dereferences it. */
static MailboxOwnedSlot s_ack_slots[T5AI_MAILBOX_CHANNEL_COUNT];
static MailboxOwnedSlot s_reset_storage;
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

#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
#define MAILBOX_TIMEOUT_TRACE_MAGIC 0x4d425402u
#define MAILBOX_PM_CHANNEL_INDEX    2u // Pinned PWC transmit channel 0x12.

typedef struct {
  uint32_t magic, channel, state, sequence, header, parameter1, parameter2, parameter3;
  uint32_t age, control, peer_fifo, routes, enabled, pending, interrupt_mask, priority_mask,
      checksum;
} TimeoutTrace;

static TimeoutTrace s_timeout_trace __attribute__((section(".t5ai_retained"), aligned(32)));

static uint32_t prv_timeout_checksum(const TimeoutTrace *trace) {
  const uint32_t *words = &trace->channel;
  uint32_t checksum = MAILBOX_TIMEOUT_TRACE_MAGIC;
  for (size_t i = 0;
       i < (offsetof(TimeoutTrace, checksum) - offsetof(TimeoutTrace, channel)) / sizeof(*words);
       ++i)
    checksum = (checksum << 5 | checksum >> 27) ^ words[i];
  return checksum;
}

static void prv_save_timeout(uint8_t channel) {
  const TxChannel *tx = &s_tx[channel];
  const TimeoutTrace trace = {
    .channel = channel,
    .state = tx->state,
    .sequence = tx->sequence,
    .header = tx->message.header,
    .parameter1 = tx->message.parameter1,
    .parameter2 = tx->message.parameter2,
    .parameter3 = tx->message.parameter3,
    .age = T5AI_AON_RTC_COUNT_LOW - tx->active_start,
    .control = MBOX_CH1_CTRL,
    .peer_fifo = MBOX_CH0_FIFO_STATUS,
    .routes = SYSTEM_CPU1_INT_32_63_ENABLE,
    .enabled = NVIC->ISER[1],
    .pending = NVIC->ISPR[1],
    .interrupt_mask = __get_PRIMASK(),
    .priority_mask = __get_BASEPRI(),
  };
  s_timeout_trace = trace;
  s_timeout_trace.checksum = prv_timeout_checksum(&s_timeout_trace);
  t5ai_cache_clean(&s_timeout_trace, sizeof(s_timeout_trace));
  s_timeout_trace.magic = MAILBOX_TIMEOUT_TRACE_MAGIC;
  t5ai_cache_clean(&s_timeout_trace, sizeof(s_timeout_trace));
}

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_IRQ_DIAGNOSTIC
#define SYSTEM_CPU0_MAILBOX_ROUTE  (*(volatile uint32_t *)0x44010084u)
#define SYSTEM_CPU0_CONTROL        (*(volatile uint32_t *)0x44010010u)
#define SYSTEM_CPU_RUN_STATUS      (*(volatile uint32_t *)0x4401000cu)
#define SYSTEM_CPU0_INTERRUPT_MASK (1u << 2)
#define SYSTEM_CPU0_HALTED         (1u << 0)

static uint8_t prv_cp_irq_reset_reason(void) {
  uint8_t reason = T5AI_RESET_REASON_PM_FIFO_BASE;
  if (SYSTEM_CPU0_MAILBOX_ROUTE & MAILBOX_SYSTEM_INTERRUPT_ENABLE)
    reason |= T5AI_RESET_REASON_PM_CP_ROUTE_ENABLED;
  if (MBOX_CH0_CTRL & MBOX_INTERRUPT_ENABLE)
    reason |= T5AI_RESET_REASON_PM_CP_IRQ_ENABLED;
  if (SYSTEM_CPU0_CONTROL & SYSTEM_CPU0_INTERRUPT_MASK)
    reason |= T5AI_RESET_REASON_PM_CP_IRQ_MASKED;
  if (SYSTEM_CPU_RUN_STATUS & SYSTEM_CPU0_HALTED)
    reason |= T5AI_RESET_REASON_PM_CP_HALTED;
  return reason;
}
#endif

#if defined(CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_DIAGNOSTIC) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_RELEASE_DIAGNOSTIC)
typedef struct {
  uint32_t magic, generation, stage, inverse;
} CPStageTrace;
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_ADDRESS >= T5AI_SHARED_SRAM_BEGIN &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_ADDRESS <=
                       T5AI_SHARED_SRAM_END - T5AI_SHARED_ALIGNMENT,
               "CP trace must be inside the shared SRAM alias");
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_ADDRESS % T5AI_SHARED_ALIGNMENT == 0u,
               "CP trace must own a cache line");

static CPStageTrace prv_cp_trace_snapshot(void) {
  volatile CPStageTrace *shared =
      (volatile CPStageTrace *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_ADDRESS;
  t5ai_cache_invalidate_received((void *)shared, sizeof(*shared));
  __DSB();
  CPStageTrace trace = {shared->magic, shared->generation, shared->stage, shared->inverse};
  __DSB();
  if (shared->magic != trace.magic || shared->generation != trace.generation ||
      shared->stage != trace.stage || shared->inverse != trace.inverse)
    trace.magic = 0u;
  return trace;
}

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_DIAGNOSTIC
static uint8_t prv_cp_trace_reset_reason(uint32_t generation) {
  const CPStageTrace trace = prv_cp_trace_snapshot();
  uint8_t reason = T5AI_RESET_REASON_PM_FIFO_BASE;
  if (trace.magic != T5AI_SLEEP_TRACE_MAGIC || trace.stage != ~trace.inverse)
    return reason;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_BOUNDARY_DIAGNOSTIC
  if (trace.generation != generation)
    return reason;
  switch (trace.stage) {
    case T5AISleepStage_CP_Restored:
      return T5AI_RESET_REASON_PM_CP_BOUNDARY_STARTED;
    case T5AISleepStage_CP_AppVetoRestored:
      return T5AI_RESET_REASON_PM_CP_BOUNDARY_VETO;
    case T5AISleepStage_CP_ReadyRevoked:
      return T5AI_RESET_REASON_PM_CP_BOUNDARY_READY;
    case T5AISleepStage_CP_ResumeSent:
      return T5AI_RESET_REASON_PM_CP_BOUNDARY_SENT;
    case T5AISleepStage_CP_InterruptsReleased:
      return T5AI_RESET_REASON_PM_CP_BOUNDARY_RELEASED;
    default:
      return T5AI_RESET_REASON_PM_CP_BOUNDARY_OTHER;
  }
#endif
  reason |= T5AI_RESET_REASON_PM_CP_TRACE_VALID;
  if (trace.generation == generation)
    reason |= T5AI_RESET_REASON_PM_CP_TRACE_GENERATION;
  if (trace.stage == T5AISleepStage_CP_Restored)
    reason |= T5AI_RESET_REASON_PM_CP_TRACE_RESTORED;
  if (trace.stage == T5AISleepStage_CP_AppVetoRestored)
    reason |= T5AI_RESET_REASON_PM_CP_TRACE_VETO;
  return reason;
}
#endif
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_STATE_DIAGNOSTIC
typedef struct {
  uint32_t cancelled, prepared, resources, critical;
} CPLiveState;
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_CANCELLED_ADDRESS >= T5AI_SHARED_SRAM_BEGIN &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_CANCELLED_ADDRESS <=
                       T5AI_SHARED_SRAM_END - sizeof(uint32_t),
               "CP cancelled symbol must be in shared SRAM");
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_PREPARED_ADDRESS >= T5AI_SHARED_SRAM_BEGIN &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_PREPARED_ADDRESS <=
                       T5AI_SHARED_SRAM_END - sizeof(uint32_t),
               "CP prepared symbol must be in shared SRAM");
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_RESOURCES_ADDRESS >= T5AI_SHARED_SRAM_BEGIN &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_RESOURCES_ADDRESS <=
                       T5AI_SHARED_SRAM_END - sizeof(uint32_t),
               "CP resources symbol must be in shared SRAM");
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_ADDRESS >= T5AI_SHARED_SRAM_BEGIN &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_ADDRESS <=
                       T5AI_SHARED_SRAM_END - sizeof(uint32_t),
               "CP critical symbol must be in shared SRAM");
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_ADDRESS % sizeof(uint32_t) == 0u,
               "CP critical counter must be word aligned");

static CPLiveState prv_cp_live_state(void) {
  t5ai_cache_invalidate_received((void *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_CANCELLED_ADDRESS,
                                 sizeof(uint8_t));
  t5ai_cache_invalidate_received((void *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_PREPARED_ADDRESS,
                                 sizeof(uint8_t));
  t5ai_cache_invalidate_received((void *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_RESOURCES_ADDRESS,
                                 sizeof(uint8_t));
  t5ai_cache_invalidate_received((void *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_ADDRESS,
                                 sizeof(uint32_t));
  __DSB();
  const CPLiveState state = {
    .cancelled = *(volatile uint8_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_CANCELLED_ADDRESS,
    .prepared = *(volatile uint8_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_PREPARED_ADDRESS,
    .resources = *(volatile uint8_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_RESOURCES_ADDRESS,
    .critical = *(volatile uint32_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_ADDRESS,
  };
  return state;
}

static uint8_t prv_cp_state_reset_reason(void) {
  const CPLiveState state = prv_cp_live_state();
  return T5AI_RESET_REASON_PM_FIFO_BASE |
         (state.cancelled ? T5AI_RESET_REASON_PM_CP_CANCELLED : 0u) |
         (state.prepared ? T5AI_RESET_REASON_PM_CP_PREPARED : 0u) |
         (state.resources ? T5AI_RESET_REASON_PM_CP_RESOURCES : 0u) |
         (state.critical ? T5AI_RESET_REASON_PM_CP_CRITICAL : 0u);
}
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_DIAGNOSTIC
#define CP_CRITICAL_STABLE_TICKS         T5AI_AON_RTC_HZ
#define CP_CRITICAL_MAX_SAMPLE_GAP_TICKS (T5AI_AON_RTC_HZ / 10u)
#define CP_CRITICAL_MIN_SAMPLES          3u
typedef struct {
  uint32_t value, transaction_start, stable_start, last_tick, samples;
  bool valid;
} CPCriticalHistory;
static CPCriticalHistory s_cp_critical_history;

static void prv_cp_critical_observe(uint32_t critical, uint32_t now, uint32_t transaction_start) {
  CPCriticalHistory *history = &s_cp_critical_history;
  if (!history->valid || history->transaction_start != transaction_start ||
      history->value != critical || critical == 0u ||
      (uint32_t)(now - history->last_tick) > CP_CRITICAL_MAX_SAMPLE_GAP_TICKS) {
    *history = (CPCriticalHistory){
      .value = critical,
      .transaction_start = transaction_start,
      .stable_start = now,
      .last_tick = now,
      .samples = 1u,
      .valid = true
    };
    return;
  }
  history->last_tick = now;
  if (history->samples < CP_CRITICAL_MIN_SAMPLES)
    ++history->samples;
}

static void prv_sample_cp_critical(void) {
  const TxChannel *tx = &s_tx[MAILBOX_PM_CHANNEL_INDEX];
  if (tx->state != TxState_Active ||
      (tx->message.header & MAILBOX_HEADER_COMMAND_MASK) != T5AI_PM_NATIVE_ALARM_COMMAND ||
      tx->message.parameter3 != T5AIAlarm_Cancel) {
    s_cp_critical_history.valid = false;
    return;
  }
  const CPLiveState state = prv_cp_live_state();
  prv_cp_critical_observe(state.critical, T5AI_AON_RTC_COUNT_LOW, tx->active_start);
}

static uint8_t prv_cp_critical_reset_reason(void) {
  prv_sample_cp_critical();
  const CPCriticalHistory *history = &s_cp_critical_history;
  if (!history->valid)
    return T5AI_RESET_REASON_PM_FIFO_BASE;
  const uint32_t nesting = history->value > T5AI_RESET_REASON_PM_CP_NESTING_MAX
                               ? T5AI_RESET_REASON_PM_CP_NESTING_MAX
                               : history->value;
  const bool stable =
      history->value != 0u && history->samples >= CP_CRITICAL_MIN_SAMPLES &&
      (uint32_t)(history->last_tick - history->stable_start) >= CP_CRITICAL_STABLE_TICKS;
  return T5AI_RESET_REASON_PM_FIFO_BASE | nesting |
         (stable ? T5AI_RESET_REASON_PM_CP_NESTING_STABLE : 0u);
}
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_DIAGNOSTIC
#define CP_EXCEPTION_SYNC_OFFSET 32u
#define CP_EXCEPTION_MAGIC_HEAD  0xabcd9527u
#define CP_EXCEPTION_MAGIC_TAIL  0x9527abcdu
typedef struct {
  uint32_t magic_head, cp_entered, ap_entered, turn, dumper, magic_tail;
} CPExceptionSync;
#define CP_RUNTIME_CHECK_WORD(address)                                       \
  _Static_assert((address) >= T5AI_SHARED_SRAM_BEGIN &&                      \
                     (address) <= T5AI_SHARED_SRAM_END - sizeof(uint32_t) && \
                     (address) % sizeof(uint32_t) == 0u,                     \
                 "CP runtime word must be aligned shared SRAM")
CP_RUNTIME_CHECK_WORD(CONFIG_T5AI_RETAINED_SLEEP_CP_TASK_POINTER_ADDRESS);
CP_RUNTIME_CHECK_WORD(CONFIG_T5AI_RETAINED_SLEEP_CP_SCHEDULER_ADDRESS);
CP_RUNTIME_CHECK_WORD(CONFIG_T5AI_RETAINED_SLEEP_CP_PENDING_TICKS_ADDRESS);
CP_RUNTIME_CHECK_WORD(CONFIG_T5AI_RETAINED_SLEEP_CP_EXCEPTION_BASE_ADDRESS);
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_CLEANUP_IRQ_DIAGNOSTIC
CP_RUNTIME_CHECK_WORD(CONFIG_T5AI_RETAINED_SLEEP_CP_CLEANUP_IRQ_ADDRESS);
#define CP_CLEANUP_IRQ_RESET_MASK 0x0fu
#endif
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_EXCEPTION_BASE_ADDRESS <=
                   T5AI_SHARED_SRAM_END - CP_EXCEPTION_SYNC_OFFSET - sizeof(CPExceptionSync),
               "CP exception record must fit shared SRAM");

// CP DTCM is private: compare the ELF symbol value, never dereference it on AP.
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_IDLE_TCB_ADDRESS != 0u,
               "Bind the CP idle TCB symbol before enabling runtime diagnostics");
static bool prv_cp_idle_task(uintptr_t task) {
  return task == CONFIG_T5AI_RETAINED_SLEEP_CP_IDLE_TCB_ADDRESS;
}

static uint8_t prv_cp_runtime_reset_reason(uint32_t generation) {
  volatile uint32_t *task =
      (volatile uint32_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_TASK_POINTER_ADDRESS;
  volatile uint32_t *scheduler =
      (volatile uint32_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_SCHEDULER_ADDRESS;
  volatile uint32_t *pending =
      (volatile uint32_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_PENDING_TICKS_ADDRESS;
  volatile CPExceptionSync *exception =
      (volatile CPExceptionSync *)(uintptr_t)(CONFIG_T5AI_RETAINED_SLEEP_CP_EXCEPTION_BASE_ADDRESS +
                                              CP_EXCEPTION_SYNC_OFFSET);
  t5ai_cache_invalidate_received((void *)task, sizeof(*task));
  t5ai_cache_invalidate_received((void *)scheduler, sizeof(*scheduler));
  t5ai_cache_invalidate_received((void *)pending, sizeof(*pending));
  t5ai_cache_invalidate_received((void *)exception, sizeof(*exception));
  __DSB();
  const uint32_t task_before = *task;
  const bool idle_name = prv_cp_idle_task(task_before);
  t5ai_cache_invalidate_received((void *)task, sizeof(*task));
  __DSB();
  const bool idle_task = idle_name && task_before == *task;
  const bool cp_exception = exception->magic_head == CP_EXCEPTION_MAGIC_HEAD &&
                            exception->magic_tail == CP_EXCEPTION_MAGIC_TAIL &&
                            exception->cp_entered == 1u && exception->ap_entered <= 1u &&
                            exception->turn <= 1u &&
                            (exception->dumper == UINT32_MAX || exception->dumper <= 1u);
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_RELEASE_DIAGNOSTIC
  const CPStageTrace trace = prv_cp_trace_snapshot();
  const bool released = trace.magic == T5AI_SLEEP_TRACE_MAGIC && trace.stage == ~trace.inverse &&
                        trace.generation == generation &&
                        trace.stage == T5AISleepStage_CP_InterruptsReleased;
  const uint8_t progress = released ? T5AI_RESET_REASON_PM_CP_RUNTIME_RELEASED : 0u;
#else
  (void)generation;
  const uint8_t progress =
      *pending > CONFIG_T5AI_RETAINED_SLEEP_CP_TICK_HZ ? T5AI_RESET_REASON_PM_CP_TICK_BACKLOG : 0u;
#endif
  const uint8_t runtime_reason =
      T5AI_RESET_REASON_PM_FIFO_BASE | (cp_exception ? T5AI_RESET_REASON_PM_CP_EXCEPTION : 0u) |
      (idle_task ? T5AI_RESET_REASON_PM_CP_IDLE_TASK : 0u) |
      (*scheduler ? T5AI_RESET_REASON_PM_CP_SCHEDULER_SUSPENDED : 0u) | progress;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_CLEANUP_IRQ_DIAGNOSTIC
  (void)runtime_reason;
  volatile uint32_t *irq_state =
      (volatile uint32_t *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_CLEANUP_IRQ_ADDRESS;
  t5ai_cache_invalidate_received((void *)irq_state, sizeof(*irq_state));
  __DSB();
  return T5AI_RESET_REASON_PM_FIFO_BASE | (*irq_state & CP_CLEANUP_IRQ_RESET_MASK);
#else
  return runtime_reason;
#endif
}
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_DIAGNOSTIC
_Static_assert(CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_ADDRESS >= T5AI_SHARED_SRAM_BEGIN &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_ADDRESS <=
                       T5AI_SHARED_SRAM_END - sizeof(T5AIReceiveTrace) &&
                   CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_ADDRESS % T5AI_SLEEP_TRACE_ALIGNMENT == 0u,
               "Bind the complete aligned CP receive record from its verified ELF");

static bool prv_cp_receive_snapshot(T5AIReceiveTrace *snapshot) {
  const volatile T5AIReceiveTrace *record =
      (const volatile T5AIReceiveTrace *)(uintptr_t)CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_ADDRESS;
  t5ai_cache_invalidate_received((void *)record, sizeof(*record));
  __DSB();
  const uint32_t before = record->sequence;
  if (before & T5AI_RECEIVE_TRACE_IN_PROGRESS)
    return false;
  *snapshot = (T5AIReceiveTrace){
    .magic = record->magic,
    .sequence = record->sequence,
    .generation = record->generation,
    .stage = record->stage,
    .header = record->header,
    .payload = record->payload,
    .inverse = record->inverse,
    .version = record->version,
  };
  t5ai_cache_invalidate_received((void *)record, sizeof(*record));
  __DSB();
  return before == record->sequence && before == snapshot->sequence &&
         snapshot->inverse == ~before && snapshot->magic == T5AI_RECEIVE_TRACE_MAGIC &&
         snapshot->version == T5AI_RECEIVE_TRACE_VERSION && snapshot->generation != 0u &&
         snapshot->stage > T5AIReceive_None && snapshot->stage <= T5AIReceive_Rejected;
}

#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_OWNER_DIAGNOSTIC
static uint8_t prv_cp_receive_owner_properties(const T5AIReceiveTrace *snapshot,
                                               uint32_t owner_header, uint32_t control) {
  const uint32_t channel_mask = UINT32_MAX << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT;
  const uint32_t sequence_mask = UINT8_MAX << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT;
  return T5AI_RESET_REASON_CP_RECEIVE_BASE |
         (!((snapshot->header ^ owner_header) & channel_mask) ? T5AI_RESET_REASON_CP_RECEIVE_CHANNEL
                                                              : 0u) |
         (!((snapshot->header ^ owner_header) & sequence_mask)
              ? T5AI_RESET_REASON_CP_RECEIVE_SEQUENCE
              : 0u) |
         ((snapshot->header & MBOX_ACK_BOX) ? T5AI_RESET_REASON_CP_RECEIVE_ACK : 0u) |
         ((control & MBOX_FIFO_NOT_EMPTY) ? T5AI_RESET_REASON_CP_RECEIVE_QUEUED : 0u);
}

static uint8_t prv_cp_receive_owner_reason(uint8_t channel) {
  T5AIReceiveTrace snapshot;
  if (!prv_cp_receive_snapshot(&snapshot))
    return T5AI_RESET_REASON_CP_RECEIVE_INVALID;
  return prv_cp_receive_owner_properties(&snapshot, s_tx[channel].message.header, MBOX_CH0_CTRL);
}
#endif

static uint8_t prv_cp_receive_reset_reason(void) {
  T5AIReceiveTrace snapshot;
  return T5AI_RESET_REASON_CP_RECEIVE_BASE |
         (prv_cp_receive_snapshot(&snapshot) ? snapshot.stage : T5AIReceive_None);
}
#endif

bool t5ai_mailbox_channel_test_timeout_output(void) {
  // Reserved channel distinguishes the startup route check from a real timeout.
  TimeoutTrace trace = {.magic = MAILBOX_TIMEOUT_TRACE_MAGIC, .channel = UINT32_MAX};
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_STATE_DIAGNOSTIC
  const CPLiveState cp_state = prv_cp_live_state();
  trace.header = prv_cp_state_reset_reason();
  trace.parameter1 = cp_state.cancelled;
  trace.parameter2 = cp_state.prepared;
  trace.parameter3 = cp_state.resources;
  trace.control = cp_state.critical;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_DIAGNOSTIC
  trace.header = prv_cp_runtime_reset_reason(0u);
#endif
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_DIAGNOSTIC
  const CPStageTrace cp_trace = prv_cp_trace_snapshot();
  trace.parameter1 = cp_trace.magic;
  trace.parameter2 = cp_trace.generation;
  trace.parameter3 = cp_trace.stage;
  trace.control = cp_trace.inverse;
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_IRQ_DIAGNOSTIC
  trace.header = prv_cp_irq_reset_reason();
  trace.control = MBOX_CH0_CTRL;
  trace.routes = SYSTEM_CPU0_MAILBOX_ROUTE;
  trace.enabled = SYSTEM_CPU0_CONTROL;
  trace.pending = SYSTEM_CPU_RUN_STATUS;
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_DIAGNOSTIC
  trace.header = prv_cp_receive_reset_reason();
#endif
  trace.checksum = prv_timeout_checksum(&trace);
  uint32_t words[sizeof(trace) / sizeof(uint32_t)];
  memcpy(words, &trace, sizeof(words));
  return t5ai_fault_trace_words(words, sizeof(words) / sizeof(*words));
}

void t5ai_mailbox_channel_dump_previous_timeout(void) {
  const TimeoutTrace trace = s_timeout_trace;
  if (trace.magic != MAILBOX_TIMEOUT_TRACE_MAGIC || trace.checksum != prv_timeout_checksum(&trace))
    return;
  s_timeout_trace.magic = 0u;
  t5ai_cache_clean(&s_timeout_trace, sizeof(s_timeout_trace));
  PBL_LOG_DBG("T5SLEEP previous mailbox channel=%lu state=%lu sequence=%lu header=%lx",
              (unsigned long)trace.channel, (unsigned long)trace.state,
              (unsigned long)trace.sequence, (unsigned long)trace.header);
  PBL_LOG_DBG("T5SLEEP previous mailbox payload=%lx/%lx/%lx age=%lu",
              (unsigned long)trace.parameter1, (unsigned long)trace.parameter2,
              (unsigned long)trace.parameter3, (unsigned long)trace.age);
  PBL_LOG_DBG("T5SLEEP previous mailbox ctrl=%lx peer_fifo=%lx routes=%lx",
              (unsigned long)trace.control, (unsigned long)trace.peer_fifo,
              (unsigned long)trace.routes);
  PBL_LOG_DBG("T5SLEEP previous mailbox NVIC enabled=%lx pending=%lx", (unsigned long)trace.enabled,
              (unsigned long)trace.pending);
}
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
static uint8_t prv_timeout_reset_reason(uint8_t channel) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_OWNER_DIAGNOSTIC
  return prv_cp_receive_owner_reason(channel);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_CP_RECEIVE_DIAGNOSTIC)
  return prv_cp_receive_reset_reason();
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_RUNTIME_DIAGNOSTIC
  // A stalled non-PM client also needs the exactly bound CP service snapshot.
  return prv_cp_runtime_reset_reason(s_tx[channel].message.parameter1);
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_IRQ_DIAGNOSTIC
  // Route delivery can stall any logical owner, including the ordinary log client.
  return prv_cp_irq_reset_reason();
#endif
  if (channel != MAILBOX_PM_CHANNEL_INDEX)
    return T5AI_RESET_REASON_MAILBOX_BASE | channel;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_DIAGNOSTIC
  return prv_cp_critical_reset_reason();
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_STATE_DIAGNOSTIC
  return prv_cp_state_reset_reason();
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_TRACE_DIAGNOSTIC
  return prv_cp_trace_reset_reason(s_tx[channel].message.parameter1);
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_PM_FIFO_DIAGNOSTIC
  uint8_t reason = T5AI_RESET_REASON_PM_FIFO_BASE;
  if (MBOX_CH0_CTRL & MBOX_FIFO_NOT_EMPTY)
    reason |= T5AI_RESET_REASON_PM_CP_RX_PENDING;
  if (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY)
    reason |= T5AI_RESET_REASON_PM_AP_RX_PENDING;
  if ((SYSTEM_CPU1_INT_32_63_ENABLE & NVIC->ISER[1] & MAILBOX_SYSTEM_INTERRUPT_ENABLE) &&
      (MBOX_CH1_CTRL & MBOX_INTERRUPT_ENABLE))
    reason |= T5AI_RESET_REASON_PM_AP_ROUTE_ENABLED;
  if (__get_PRIMASK() || __get_BASEPRI())
    reason |= T5AI_RESET_REASON_PM_AP_IRQ_MASKED;
  return reason;
#endif
  const TxChannel *tx = &s_tx[channel];
  if (tx->state == TxState_Pending)
    return T5AI_RESET_REASON_SLEEP_PM_PENDING;
  if ((tx->message.header & MAILBOX_HEADER_COMMAND_MASK) != T5AI_PM_NATIVE_ALARM_COMMAND)
    return T5AI_RESET_REASON_SLEEP_PM_COMMAND;
  switch (tx->message.parameter3) {
    case T5AIAlarm_Cancel:
      return T5AI_RESET_REASON_SLEEP_PM_CANCEL;
    case T5AIAlarm_ArmSleepMaskBarrier:
      return T5AI_RESET_REASON_SLEEP_PM_PREPARE;
    case T5AIAlarm_State:
      return T5AI_RESET_REASON_SLEEP_PM_STATE;
    case T5AIAlarm_LowVoltageCount:
      return T5AI_RESET_REASON_SLEEP_PM_COUNT;
    default:
      return T5AI_RESET_REASON_SLEEP_PM_OTHER;
  }
}
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
static uint8_t prv_timeout_owner(uint8_t channel) {
  if (s_tx[channel].state == TxState_Pending) {
    for (uint8_t index = 0u; index < T5AI_MAILBOX_CHANNEL_COUNT; ++index) {
      if (s_tx[index].state == TxState_Active)
        return index;
    }
  }
  return channel;
}
#endif

[[noreturn]] static void prv_reset_after_timeout(uint8_t channel) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  // A queued caller can expire before the physical owner's own watchdog.
  const uint8_t owner = prv_timeout_owner(channel);
  const uint8_t reset_reason = prv_timeout_reset_reason(owner);
  prv_save_timeout(owner);
  // SRAM can be lost on the reset; send the record through the independent emergency CP path.
  uint32_t words[sizeof(s_timeout_trace) / sizeof(uint32_t)];
  memcpy(words, &s_timeout_trace, sizeof(words));
  (void)t5ai_fault_trace_words(words, sizeof(words) / sizeof(*words));
#endif
  t5ai_reboot_trace_event(T5RebootTrace_MailboxTimeout, channel);
  RebootReason reason = {
    .code = RebootReasonCode_MailboxTimeout,
    .extra.value = channel,
  };
  reboot_reason_set(&reason);
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  t5ai_force_whole_chip_reset(reset_reason);
#else
  t5ai_force_whole_chip_reset(T5AI_RESET_REASON_MAILBOX_BASE | channel);
#endif
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
  t5ai_cache_clean((void *)message, sizeof(*message));
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
  // CP-owned input, or a returned command in our private padded transmit slot.
  t5ai_cache_invalidate_received((void *)address, sizeof(*message));
  memcpy(message, (const void *)address, sizeof(*message));
  return true;
}

static void prv_finish_tx(TxChannel *tx, const T5AIMailboxMessage *ack, status_t status) {
  tx->ack = *ack;
  tx->state = tx->synchronous ? TxState_Complete : TxState_Idle;
  // Also wake a synchronous client waiting for an asynchronous slot to retire.
  (void)pbl_sem_give(&tx->completion);
  if (!tx->synchronous && tx->handler != NULL) {
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
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_block(T5AISleepOwner_Mailbox);
#endif
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
  const bool pending = (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u;
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
  t5ai_sleep_unblock(T5AISleepOwner_Mailbox);
#endif
  return pending;
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
    const uint32_t async_ms = t5ai_mb_ipc_async_poll();
    const pbl_tick_t async_wait =
        async_ms == UINT32_MAX ? PBL_TICK_FOREVER : prv_milliseconds_to_ticks(async_ms);
    pbl_tick_t wait = heartbeat_wait < fault_wait ? heartbeat_wait : fault_wait;
    if (async_wait < wait) {
      wait = async_wait;
    }
    (void)pbl_sem_take(&s_wake, PBL_TICKS(wait));
    const bool rx_pending = prv_drain_rx();
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_CRITICAL_DIAGNOSTIC
    prv_sample_cp_critical();
#endif
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
  s_reset_storage.message = (T5AIMailboxMessage){
    .header = ((uint32_t)T5AI_MAILBOX_RX_CHANNEL_BASE << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT) |
              MBOX_ACK_BOX | MBOX_RESET,
  };
  if (prv_publish(&s_reset_storage.message) == S_SUCCESS) {
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
  if (pbl_mutex_lock(&tx->mutex, PBL_TICKS(0u)) != 0) {
    return E_BUSY;
  }
  pbl_irq_lock();
  if (tx->state != TxState_Idle) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&tx->mutex);
    return E_BUSY;
  }
  tx->message = *message;
  tx->handler = handler;
  tx->context = context;
  tx->synchronous = false;
  tx->state = TxState_Pending;
  pbl_irq_unlock();
  pbl_mutex_unlock(&tx->mutex);
  (void)pbl_sem_give(&s_wake);
  return S_SUCCESS;
}

status_t t5ai_mailbox_channel_send(uint8_t channel, const T5AIMailboxMessage *message,
                                   T5AIMailboxMessage *ack, uint32_t timeout_ms) {
  if (t5ai_mailbox_channel_is_owner()) {
    return E_INVALID_OPERATION;
  }
  uint8_t index;
  if (!s_started || s_faulted || !prv_tx_channel(channel, &index) || message == NULL) {
    return E_INVALID_ARGUMENT;
  }
  TxChannel *tx = &s_tx[index];
  pbl_mutex_lock(&tx->mutex, PBL_FOREVER);
  while (pbl_sem_take(&tx->completion, PBL_TICKS(0u)) == 0) {
  }
  // Holding the native mutex prevents asynchronous clients stealing the next slot.
  for (;;) {
    pbl_irq_lock();
    if (tx->state == TxState_Idle) {
      break;
    }
    pbl_irq_unlock();
    if (pbl_sem_take(&tx->completion,
                     PBL_TICKS(prv_milliseconds_to_ticks(MAILBOX_ACTIVE_TIMEOUT_MS))) != 0) {
      s_faulted = true;
      prv_reset_after_timeout(index);
    }
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

bool t5ai_mailbox_channel_is_owner(void) {
  return !mcu_state_is_isr() && pbl_thread_current() == &s_task;
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
  static MailboxOwnedSlot boot_storage;
  boot_storage.message = *message;
  ++s_sequence;
  boot_storage.message.header = (boot_storage.message.header & MAILBOX_HEADER_COMMAND_MASK) |
                                ((uint32_t)s_sequence << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) |
                                ((uint32_t)channel << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
  const uint32_t poll_ms = timeout_ms == 0u ? MAILBOX_BOOT_POLL_MS : timeout_ms;
  prv_bootstrap_resynchronize(poll_ms);
  if (!prv_bootstrap_publish(&boot_storage.message, poll_ms)) {
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

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
bool t5ai_mailbox_channel_sleep_ready(void) {
  // Called with IRQs masked; preserve quarantined slots and pending receive ACKs.
  if (!s_started || s_faulted || s_pending_ack_mask != 0u ||
      (MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u)
    return false;
  for (unsigned i = 0; i < T5AI_MAILBOX_CHANNEL_COUNT; ++i) {
    if (s_tx[i].state != TxState_Idle)
      return false;
  }
  return true;
}
#endif
