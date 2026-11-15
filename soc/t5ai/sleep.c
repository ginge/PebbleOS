/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rtc.h>
#include <pbl/kernel/cache.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/section.h>
#include <pbl/kernel/time.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/memory.h>
#include <pbl/soc/t5ai/native_alarm.h>
#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/sleep.h>
#include <pbl/soc/t5ai/tick.h>

#ifdef CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY
#include <pbl/kernel/debug.h>
#include <pbl/kernel/time.h>
#include <pbl/soc/t5ai/sleep_deadline_profile.h>
#endif

#include <pbl/soc/t5ai/core.h>

#if !defined(CONFIG_T5AI_RETAINED_SLEEP_TEST) && !defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
#error "Production retained idle requires CP-awake AP-only sleep"
#endif

#define SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS  0x44010088u
#define SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS 0x4401008cu
#define SLEEP_AP_IRQ_STATUS_LOW_ADDRESS  0x440100a8u
#define SLEEP_AP_IRQ_STATUS_HIGH_ADDRESS 0x440100acu
#define SLEEP_AP_MAILBOX_INTERRUPT       (1u << 31)
#define SLEEP_AP_RTC_INTERRUPT           (1u << 22)
#define SLEEP_AP_GPIO_INTERRUPT          (1u << 23)
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_WAKE_ONLY
#define SLEEP_AP_WAKE_INTERRUPTS          SLEEP_AP_MAILBOX_INTERRUPT
#define SLEEP_AP_EXTERNAL_WAKE_INTERRUPTS 0u
#else
#define SLEEP_AP_WAKE_INTERRUPTS          (SLEEP_AP_MAILBOX_INTERRUPT | SLEEP_AP_RTC_INTERRUPT)
#define SLEEP_AP_EXTERNAL_WAKE_INTERRUPTS SLEEP_AP_GPIO_INTERRUPT
#endif
#define SLEEP_TEST_SLOT_SECONDS         20u
#define SLEEP_CPU_STATUS_ADDRESS        0x4401000cu
#define SLEEP_SIBLING_CONTROL_ADDRESS   0x44010018u
#define SLEEP_AON_COORDINATION_ADDRESS  0x4400000cu
#define SLEEP_CORE_CLOCK_MODE_ADDRESS   0x44010020u
#define SLEEP_AP_CLOCK_CONTROL_ADDRESS  0x44010014u
#define SLEEP_CP_CLOCK_CONTROL_ADDRESS  0x44010010u
#define SLEEP_MEMORY_CLOCK_MODE_ADDRESS 0x44010024u
#define SLEEP_CORE_BUS_CLOCK_MASK       0x7fu
#define SLEEP_FLASH_CLOCK_MASK          (15u << 24)
#define SLEEP_PSRAM_CLOCK_MASK          (3u << 4)
#define SLEEP_MEMORY_CLOCK_MASK         (SLEEP_FLASH_CLOCK_MASK | SLEEP_PSRAM_CLOCK_MASK)
#define SLEEP_CP_READY                  (1u << 3)
#define SLEEP_AP_WFI                    (1u << 1)
#define SLEEP_CLOCK_SOURCE_SHIFT        4u
#define SLEEP_CLOCK_SOURCE_MASK         3u
#define SLEEP_CLOCK_DIVIDER_MASK        15u
#define SLEEP_CPU_FULL_SPEED            (1u << 4)
#define SLEEP_DPLL_ENABLE               (1u << 5)
#define SLEEP_SOURCE_DPLL_320           2u
#define SLEEP_SOURCE_DPLL_480           3u
#define SLEEP_DPLL_320_HZ               320000000u
#define SLEEP_DPLL_480_HZ               480000000u
#define SLEEP_ACTIVE_MIN_HZ             240000000u
#define SLEEP_HALF_SPEED_DIVISOR        2u
#define SLEEP_MILLISECONDS_PER_SECOND   1000u
#define SLEEP_AON_PER_MS                (T5AI_AON_RTC_HZ / SLEEP_MILLISECONDS_PER_SECOND)
#ifdef CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST
#define SLEEP_ANALOG_PUBLICATION_RESERVE_MS 2u
#define SLEEP_MIN_WORKER_WINDOW_MS \
  (T5AI_SLEEP_ANALOG_WAKE_MIN_MS + SLEEP_PREPARE_BUDGET_MS + SLEEP_WAKE_MARGIN_MS)
#define SLEEP_MIN_WINDOW_MS                                                         \
  (T5AI_SLEEP_ANALOG_WAKE_MIN_MS + SLEEP_PREPARE_BUDGET_MS + SLEEP_WAKE_MARGIN_MS + \
   SLEEP_ANALOG_PUBLICATION_RESERVE_MS)
#else
#define SLEEP_MIN_WINDOW_MS        20u
#define SLEEP_MIN_WORKER_WINDOW_MS SLEEP_MIN_WINDOW_MS
#endif
#define SLEEP_MAX_WINDOW_MS 100u
#ifdef CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST
#define SLEEP_PREPARE_BUDGET_MS T5AI_SLEEP_ANALOG_WAKE_REQUEST_BUDGET_MS
#else
#define SLEEP_PREPARE_BUDGET_MS 8u
#endif
#define SLEEP_WAKE_MARGIN_MS      8u
#define SLEEP_ENTRY_LEAD_MS       2u
#define SLEEP_RESUME_TIMEOUT_MS   700u
#define SLEEP_CP_EVENT_TIMEOUT_MS 1000u
#define SLEEP_START_DELAY_MS      30000u
#ifndef CONFIG_T5AI_RETAINED_SLEEP_TEST_REQUEST_LIMIT
#define CONFIG_T5AI_RETAINED_SLEEP_TEST_REQUEST_LIMIT 24u
#endif
#ifndef CONFIG_T5AI_RETAINED_SLEEP_TEST_ENTRY_LIMIT
#define CONFIG_T5AI_RETAINED_SLEEP_TEST_ENTRY_LIMIT 12u
#endif
#define SLEEP_TEST_REQUEST_LIMIT CONFIG_T5AI_RETAINED_SLEEP_TEST_REQUEST_LIMIT
#define SLEEP_TEST_ENTRY_LIMIT   CONFIG_T5AI_RETAINED_SLEEP_TEST_ENTRY_LIMIT
#define SLEEP_TRACE_COMPLETE     (T5AISleepStage_AP_ClocksRestored + 1u)
#define SLEEP_CANARY_WORDS       16u
#define SLEEP_CANARY_PATTERN     0x6a35c981u
#define SLEEP_MMIO(address)      (*(volatile uint32_t *)(address))

PBL_LOG_MODULE_DEFINE(t5ai_sleep, LOG_LEVEL_DEBUG);
_Static_assert((SLEEP_MIN_WINDOW_MS - SLEEP_WAKE_MARGIN_MS - SLEEP_PREPARE_BUDGET_MS) *
                       SLEEP_AON_PER_MS >=
                   T5AI_PM_NATIVE_ALARM_MIN_TICKS,
               "Minimum idle window must include alarm setup and wake margins");

typedef enum {
  SleepOff,
  SleepIdle,
  SleepPrepareQueued,
  SleepArmed,
  SleepPrepared,
  SleepCleanupQueued,
  SleepNeedsCleanup,
  SleepFailed,
  SleepTestComplete,
  SleepAwaitController
} SleepPhase;
static SleepPhase s_phase;
static bool s_initialized, s_registered;
static uint32_t s_target, s_generation, s_epoch, s_earliest_wake, s_latest_wake;
static T5AISleepStats s_stats;
static uint32_t s_controller_wait_start;
static bool s_controller_wake_queued;
// Private aligned PSRAM cache lines; display transfers never address this object.
static uint32_t s_psram_canary[SLEEP_CANARY_WORDS]
    __attribute__((section(".t5ai_display_dma"), aligned(T5AI_SHARED_ALIGNMENT)));
static uint32_t s_canary_generation;
static uint32_t s_cp_entries_start;
typedef struct {
  uint32_t magic, generation, stage, inverse;
} SleepTrace;
static volatile SleepTrace s_sleep_trace
    __attribute__((section(".t5ai_sleep_trace"), aligned(T5AI_SHARED_ALIGNMENT)));
// SRAM-only cache clean: no flash function may be called after AP publication.
#define SLEEP_MARK_STAGE(value)                         \
  do {                                                  \
    s_sleep_trace.stage = (value);                      \
    s_sleep_trace.inverse = ~(uint32_t)(value);         \
    s_sleep_trace.magic = T5AI_SLEEP_TRACE_MAGIC;       \
    __DSB();                                            \
    SCB->DCCMVAC = (uint32_t)(uintptr_t)&s_sleep_trace; \
    __DSB();                                            \
  } while (0)

#if defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
static uint32_t s_cp_mask_barriers_start;
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY) ||          \
    defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
static uint32_t s_cp_clock_start;
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
static uint32_t s_cp_resources_start;
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY) && !defined(CONFIG_T5AI_RETAINED_SLEEP_TEST)
#define SLEEP_AP_ONLY_REPORT_RETURNS 32u
static bool s_ap_only_reported, s_ap_only_settled;
static uint32_t s_ap_only_started;
#endif

#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
static uint32_t s_cp_samples_start;
static uint32_t s_ap_sleep_control_before, s_ap_sleep_control_prepared, s_ap_architecture_rejected;
static uint32_t s_test_not_before;
static uint32_t s_request_latency_max_aon, s_suspend_duration_max_aon;
static uint32_t s_idle_budget_max_aon, s_worker_budget_max_aon, s_worker_budget_refusals;
static uint32_t s_entry_headroom_min_aon = UINT32_MAX;
static bool s_test_report_queued, s_test_reported;

#ifdef CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY
// Existing kernel read-only query; called only with its required IRQ lock held.
extern pbl_tick_t timer_ticks_until_next(pbl_tick_t now);
static uint32_t s_budget_calls, s_budget_max_aon, s_budget_timer_ticks;
static uint32_t s_budget_thread_ticks, s_budget_thread_pc;
static pbl_tick_t s_budget_kernel_now;
static T5AISleepTimerCandidate s_budget_candidates[T5AI_SLEEP_PROFILE_CANDIDATES]
    __attribute__((section(".t5ai_board_cache"), aligned(T5AI_SHARED_ALIGNMENT)));
static unsigned s_budget_candidate_count, s_budget_untracked;
static char s_budget_thread_name[PBL_THREAD_NAME_LEN];

static void prv_budget_thread(const struct pbl_thread_info *info, void *context) {
  (void)context;
  if (!info->thread->backend.on_timeout_list)
    return;
  const int32_t delta = (int32_t)(info->thread->backend.wake_at - s_budget_kernel_now);
  const uint32_t remaining = delta > 0 ? (uint32_t)delta : 0u;
  if (remaining >= s_budget_thread_ticks)
    return;
  s_budget_thread_ticks = remaining;
  s_budget_thread_pc = info->regs[PBL_THREAD_REG_PC];
  for (unsigned i = 0; i < sizeof(s_budget_thread_name) - 1u; ++i) {
    s_budget_thread_name[i] = info->name[i];
    if (!info->name[i])
      break;
  }
  s_budget_thread_name[sizeof(s_budget_thread_name) - 1u] = '\0';
}

static void prv_budget_observe(uint32_t deadline) {
  pbl_irq_lock();
  ++s_budget_calls;
  const int32_t gap = (int32_t)(deadline - T5AI_AON_RTC_COUNT_LOW);
  if (gap > 0 && (uint32_t)gap > s_budget_max_aon) {
    s_budget_max_aon = (uint32_t)gap;
    s_budget_kernel_now = (pbl_tick_t)pbl_uptime_ticks();
    s_budget_timer_ticks = timer_ticks_until_next(s_budget_kernel_now);
    s_budget_thread_ticks = UINT32_MAX;
    s_budget_thread_name[0] = '\0';
    s_budget_thread_pc = 0u;
    pbl_thread_foreach(prv_budget_thread, NULL);
    s_budget_candidate_count = t5ai_sleep_profile_timer_candidates(s_budget_candidates);
    s_budget_untracked = t5ai_sleep_profile_timers_untracked();
  }
  pbl_irq_unlock();
}
#endif

static bool prv_test_returns_complete(void) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL
  if (s_stats.cp_mask_barriers)
    return true;
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST
  // Bounded safe refusals may precede the first actual CP WFI return.
  if (s_stats.cp_entries)
    return true;
#endif
  return s_stats.entries >= SLEEP_TEST_ENTRY_LIMIT;
}

static void prv_test_report(void *context) {
  (void)context;
  s_test_report_queued = false;
  if (s_test_reported)
    return;
  s_test_reported = true;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY
  PBL_LOG_DBG(
      "T5SLEEP budget calls=%lu max_aon=%lu timer_ticks=%lu thread_ticks=%lu name=%s pc=%lx",
      (unsigned long)s_budget_calls, (unsigned long)s_budget_max_aon,
      (unsigned long)s_budget_timer_ticks, (unsigned long)s_budget_thread_ticks,
      s_budget_thread_name, (unsigned long)s_budget_thread_pc);
  PBL_LOG_DBG("T5SLEEP timer estimates untracked=%u", s_budget_untracked);
  for (unsigned i = 0; i < s_budget_candidate_count; ++i) {
    const T5AISleepTimerCandidate *candidate = &s_budget_candidates[i];
    PBL_LOG_DBG("T5SLEEP timer estimate id=%lx remain_ms=%lu period_ms=%lu cb=%lx ctx=%lx",
                (unsigned long)candidate->id, (unsigned long)candidate->remaining_ms,
                (unsigned long)candidate->period_ms, (unsigned long)candidate->callback,
                (unsigned long)candidate->context);
  }

#endif
  PBL_LOG_DBG("T5SLEEP budgets idle_max=%lu worker_max=%lu worker_refused=%lu",
              (unsigned long)s_idle_budget_max_aon, (unsigned long)s_worker_budget_max_aon,
              (unsigned long)s_worker_budget_refusals);
  PBL_LOG_DBG("T5SLEEP timing request_max=%lu suspend_max=%lu headroom_min=%lu",
              (unsigned long)s_request_latency_max_aon, (unsigned long)s_suspend_duration_max_aon,
              (unsigned long)s_entry_headroom_min_aon);
  PBL_LOG_DBG("T5SLEEP slot requests=%lu prepared=%lu entries=%lu aborted=%lu dropped=%lu",
              (unsigned long)s_stats.requests, (unsigned long)s_stats.prepared,
              (unsigned long)s_stats.entries, (unsigned long)s_stats.aborted,
              (unsigned long)s_stats.dropped);
  PBL_LOG_DBG("T5SLEEP AP architecture before=%lx prepared=%lx rejected=%lu",
              (unsigned long)s_ap_sleep_control_before, (unsigned long)s_ap_sleep_control_prepared,
              (unsigned long)s_ap_architecture_rejected);
  PBL_LOG_DBG("T5SLEEP slot phase=%u fault=%lu reject=%lx APticks=%lu", (unsigned)s_phase,
              (unsigned long)s_stats.fault, (unsigned long)s_stats.rejected,
              (unsigned long)s_stats.aon_ticks);
  if (!s_initialized)
    return;
  const T5AIAlarmOperation operations[] = {
    T5AIAlarm_AnalogPrepareLeadTicks,
    T5AIAlarm_AnalogPrepareReject,
    T5AIAlarm_LowVoltageCount,
    T5AIAlarm_ReadySamples,
    T5AIAlarm_MissingVotesLow,
    T5AIAlarm_MissingVotesHigh,
    T5AIAlarm_HalReject,
    T5AIAlarm_HalPendingLow,
    T5AIAlarm_HalPendingHigh,
    T5AIAlarm_HalStatusLow,
    T5AIAlarm_HalStatusHigh,
    T5AIAlarm_PrepareTicks,
    T5AIAlarm_LowVoltageTicks,
    T5AIAlarm_Stage,
    T5AIAlarm_ResourceCount,
    T5AIAlarm_ClockCount,
    T5AIAlarm_FinalLeadTicks,
    T5AIAlarm_SleepConfig,
    T5AIAlarm_WakeConfig,
    T5AIAlarm_ClockModeLow,
    T5AIAlarm_HighFrequencyLow,
    T5AIAlarm_FinalPendingLow,
    T5AIAlarm_FinalPendingHigh,
    T5AIAlarm_FinalNvicLow,
    T5AIAlarm_FinalNvicHigh,
    T5AIAlarm_FinalReject,
    T5AIAlarm_FinalSkips,
    T5AIAlarm_MaskBefore,
    T5AIAlarm_MaskAfter,
    T5AIAlarm_MaskAtFinal,
    T5AIAlarm_FinalAPPendingLow,
    T5AIAlarm_FinalAPPendingHigh,
    T5AIAlarm_FinalGPIOStatusLow,
    T5AIAlarm_FinalGPIOStatusHigh,
    T5AIAlarm_PowerParameter,
    T5AIAlarm_WakeStatusBefore,
    T5AIAlarm_WakeStatusAfter,
    T5AIAlarm_PMUWakeCount,
    T5AIAlarm_PMUWakeSources,
    T5AIAlarm_PMUWakeTicks,
    T5AIAlarm_PMUAmbiguousCount,
    T5AIAlarm_WakeClearBefore,
    T5AIAlarm_WakeClearAfter,
    T5AIAlarm_PMUClearHeldCount,
    T5AIAlarm_HardwareMaskAfterWFI,
    T5AIAlarm_CleanupIdleRefusals,
    T5AIAlarm_CleanupIRQState,
    T5AIAlarm_CleanupIRQKicks,
    T5AIAlarm_CleanupIRQDelivery,
    T5AIAlarm_CleanupMailboxISRs,
    T5AIAlarm_CleanupMailboxEmptyStatus,
    T5AIAlarm_CleanupMailboxProgress,
    T5AIAlarm_NormalSleepSkips,
    T5AIAlarm_PMUPolicyBefore,
    T5AIAlarm_PMUPolicyAfter,
    T5AIAlarm_IRQEnableLowBefore,
    T5AIAlarm_IRQEnableLowAfter,
    T5AIAlarm_IRQEnableHighBefore,
    T5AIAlarm_IRQEnableHighAfter,
    T5AIAlarm_PeripheralClocksBefore,
    T5AIAlarm_PeripheralClocksAfter,
    T5AIAlarm_AnalogWakeState,
  };
  for (unsigned i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i) {
    uint32_t value = 0u;
    const status_t status = t5ai_native_alarm_cp_query(operations[i], &value);
    PBL_LOG_DBG("T5SLEEP slot CP op=%u value=%lx status=%ld", (unsigned)operations[i],
                (unsigned long)value, (long)status);
  }
}

static bool prv_test_window_open(void) {
  const time_t slot = CONFIG_T5AI_RETAINED_SLEEP_TEST_AT_UTC;
  const time_t now = pbl_realtime();
  return slot && now >= slot && (uint32_t)(now - slot) < SLEEP_TEST_SLOT_SECONDS;
}
#endif

static bool prv_clock_ready(uint32_t mode, uint32_t cpu) {
  const uint32_t source = (mode >> SLEEP_CLOCK_SOURCE_SHIFT) & SLEEP_CLOCK_SOURCE_MASK;
  const uint32_t source_hz = source == SLEEP_SOURCE_DPLL_320   ? SLEEP_DPLL_320_HZ
                             : source == SLEEP_SOURCE_DPLL_480 ? SLEEP_DPLL_480_HZ
                                                               : 0u;
  uint32_t divisor = (mode & SLEEP_CLOCK_DIVIDER_MASK) + 1u;
  if (!(cpu & SLEEP_CPU_FULL_SPEED))
    divisor *= SLEEP_HALF_SPEED_DIVISOR;
  return (uint64_t)source_hz >= (uint64_t)SLEEP_ACTIVE_MIN_HZ * divisor;
}

// Every instruction and literal between publication and restored-clock proof
// must stay in SRAM. No flash, kernel, cache or mailbox calls are permitted here.
PBL_SECTION_RAM static bool prv_retained_wfi(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  // The CP owns RTC acknowledgement. Borrow only an unused AP NVIC route.
  if ((NVIC->ISER[1] | NVIC->ISPR[1]) & SLEEP_AP_RTC_INTERRUPT) {
    __set_PRIMASK(primask);
    return false;
  }
  const uint32_t saved_irq_low = SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS);
  const uint32_t saved_irq_high = SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS);
  const uint32_t pending_low = SLEEP_MMIO(SLEEP_AP_IRQ_STATUS_LOW_ADDRESS);
  const uint32_t pending_high = SLEEP_MMIO(SLEEP_AP_IRQ_STATUS_HIGH_ADDRESS);
  if (saved_irq_high & SLEEP_AP_RTC_INTERRUPT) {
    __set_PRIMASK(primask);
    return false;
  }
  SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS) = 0u;
  SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS) =
      SLEEP_AP_WAKE_INTERRUPTS | (saved_irq_high & SLEEP_AP_EXTERNAL_WAKE_INTERRUPTS);
  // NVIC set/clear aliases avoid calls to flash-resident CMSIS wrappers.
  NVIC->ISER[1] = SLEEP_AP_RTC_INTERRUPT;
  __DSB();
  __ISB();
  if (pending_low || pending_high || (NVIC->ISPR[0] & NVIC->ISER[0]) ||
      (NVIC->ISPR[1] & NVIC->ISER[1]) ||
      (SCB->ICSR & (SCB_ICSR_VECTPENDING_Msk | SCB_ICSR_PENDSTSET_Msk))) {
    SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS) = saved_irq_low;
    SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS) = saved_irq_high;
    NVIC->ICER[1] = SLEEP_AP_RTC_INTERRUPT;
    NVIC->ICPR[1] = SLEEP_AP_RTC_INTERRUPT;
    __DSB();
    __set_PRIMASK(primask);
    return false;
  }
  const uint32_t start = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t saved_mode = SLEEP_MMIO(SLEEP_CORE_CLOCK_MODE_ADDRESS) & SLEEP_CORE_BUS_CLOCK_MASK;
  const uint32_t saved_ap = SLEEP_MMIO(SLEEP_AP_CLOCK_CONTROL_ADDRESS) & SLEEP_CPU_FULL_SPEED;
  const uint32_t saved_cp = SLEEP_MMIO(SLEEP_CP_CLOCK_CONTROL_ADDRESS) & SLEEP_CPU_FULL_SPEED;
  const uint32_t saved_flash =
      SLEEP_MMIO(SLEEP_MEMORY_CLOCK_MODE_ADDRESS) & SLEEP_MEMORY_CLOCK_MASK;
  const uint32_t scr = SCB->SCR;
  SCB->SCR = scr | SCB_SCR_SLEEPDEEP_Msk;
  __DSB();
  __ISB();
  const uint32_t prepared_sleep_control = SCB->SCR;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  s_ap_sleep_control_before = scr;
  s_ap_sleep_control_prepared = prepared_sleep_control;
#endif
  if (!(prepared_sleep_control & SCB_SCR_SLEEPDEEP_Msk)) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
    ++s_ap_architecture_rejected;
#endif
    SCB->SCR = scr;
    SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS) = saved_irq_low;
    SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS) = saved_irq_high;
    NVIC->ICER[1] = SLEEP_AP_RTC_INTERRUPT;
    NVIC->ICPR[1] = SLEEP_AP_RTC_INTERRUPT;
    __DSB();
    __set_PRIMASK(primask);
    return false;
  }
  // Publication grants CP permission to change clocks; architectural setup is complete.
  SLEEP_MMIO(SLEEP_AON_COORDINATION_ADDRESS) |= SLEEP_AP_WFI;
  SLEEP_MARK_STAGE(T5AISleepStage_AP_Published);
  __DSB();
#ifndef CONFIG_T5AI_RETAINED_SLEEP_AP_ACTIVE_WAIT
  __WFI();
  __ISB();
  SLEEP_MARK_STAGE(T5AISleepStage_AP_Woke);
#endif
  for (;;) {
    const uint32_t coordination = SLEEP_MMIO(SLEEP_AON_COORDINATION_ADDRESS);
    const uint32_t mode = SLEEP_MMIO(SLEEP_CORE_CLOCK_MODE_ADDRESS);
    const uint32_t cpu = SLEEP_MMIO(SLEEP_AP_CLOCK_CONTROL_ADDRESS);
    const uint32_t cp = SLEEP_MMIO(SLEEP_CP_CLOCK_CONTROL_ADDRESS);
    const uint32_t memory = SLEEP_MMIO(SLEEP_MEMORY_CLOCK_MODE_ADDRESS);
    uint8_t blockers = 0u;
    if (coordination & SLEEP_CP_READY)
      blockers |= T5AI_RESET_REASON_SLEEP_WAIT_READY;
    if ((mode & SLEEP_CORE_BUS_CLOCK_MASK) != saved_mode ||
        (cpu & SLEEP_CPU_FULL_SPEED) != saved_ap || (cp & SLEEP_CPU_FULL_SPEED) != saved_cp)
      blockers |= T5AI_RESET_REASON_SLEEP_WAIT_CORE;
    if ((memory & SLEEP_MEMORY_CLOCK_MASK) != saved_flash)
      blockers |= T5AI_RESET_REASON_SLEEP_WAIT_MEMORY;
    if (!blockers)
      break;
    if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - start) >= SLEEP_RESUME_TIMEOUT_MS * SLEEP_AON_PER_MS)
      t5ai_force_whole_chip_reset_from_sram(T5AI_RESET_REASON_SLEEP_WAIT_BASE | blockers);
  }
  SLEEP_MARK_STAGE(T5AISleepStage_AP_ClocksRestored);
  // CP stops writing ready after revocation until this AP issues its next request.
  SLEEP_MMIO(SLEEP_AON_COORDINATION_ADDRESS) &= ~SLEEP_AP_WFI;
  SCB->SCR = scr;
  SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS) = saved_irq_low;
  SLEEP_MMIO(SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS) = saved_irq_high;
  NVIC->ICER[1] = SLEEP_AP_RTC_INTERRUPT;
  NVIC->ICPR[1] = SLEEP_AP_RTC_INTERRUPT;
  __DSB();
  __ISB();
  __set_PRIMASK(primask);
  return true;
}

static void prv_canary_prepare(uint32_t generation) {
  s_canary_generation = generation;
  for (unsigned i = 0; i < SLEEP_CANARY_WORDS; ++i)
    s_psram_canary[i] = SLEEP_CANARY_PATTERN ^ generation ^ i;
  pbl_dcache_flush_range(s_psram_canary, sizeof(s_psram_canary));
}

static bool prv_canary_valid(void) {
  pbl_dcache_invalidate_range(s_psram_canary, sizeof(s_psram_canary));
  for (unsigned i = 0; i < SLEEP_CANARY_WORDS; ++i)
    if (s_psram_canary[i] != (SLEEP_CANARY_PATTERN ^ s_canary_generation ^ i))
      return false;
  return true;
}

static void prv_fail(unsigned reason) {
  t5ai_sleep_fault();
  s_stats.fault = reason;
  s_phase = SleepFailed;
  PBL_LOG_ERR("T5SLEEP failed reason=%u gen=%lu", reason, (unsigned long)s_generation);
}

// Validate the awake contract before an idle deadline starts running.
static bool prv_initialize_contract(bool startup) {
  if (s_initialized)
    return true;
  uint32_t cp_state = 0u;
  const status_t status = t5ai_native_alarm_init();
  const status_t query_status =
      status == S_SUCCESS ? t5ai_native_alarm_cp_query(T5AIAlarm_State, &cp_state) : status;
  if (startup && status == S_SUCCESS && query_status == S_SUCCESS &&
      (cp_state & T5AI_SLEEP_REQUIRED_STATE) != T5AI_SLEEP_REQUIRED_STATE) {
    s_stats.cp_state = cp_state;
    return false; // Board/peripheral initialization can still be acquiring its votes.
  }
  if (status != S_SUCCESS || query_status != S_SUCCESS ||
      (cp_state & T5AI_SLEEP_REQUIRED_STATE) != T5AI_SLEEP_REQUIRED_STATE) {
    s_stats.cp_state = cp_state;
    PBL_LOG_ERR("T5SLEEP contract init=%ld query=%ld state=%lx sibling=%lx status=%lx",
                (long)status, (long)query_status, (unsigned long)cp_state,
                (unsigned long)SLEEP_MMIO(SLEEP_SIBLING_CONTROL_ADDRESS),
                (unsigned long)SLEEP_MMIO(SLEEP_CPU_STATUS_ADDRESS));
    prv_fail(T5AISleepFault_Contract);
    return false;
  }
  s_stats.cp_state = cp_state;
#if defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
  if (t5ai_native_alarm_cp_query(T5AIAlarm_MaskBarrierCount, &s_cp_mask_barriers_start) !=
      S_SUCCESS) {
    prv_fail(T5AISleepFault_Controller);
    return false;
  }
#endif
  if (t5ai_native_alarm_cp_query(T5AIAlarm_LowVoltageCount, &s_cp_entries_start) != S_SUCCESS) {
    prv_fail(T5AISleepFault_Controller);
    return false;
  }
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (t5ai_native_alarm_cp_query(T5AIAlarm_ReadySamples, &s_cp_samples_start) != S_SUCCESS) {
    prv_fail(T5AISleepFault_Controller);
    return false;
  }
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
  if (t5ai_native_alarm_cp_query(T5AIAlarm_ResourceCount, &s_cp_resources_start) != S_SUCCESS) {
    prv_fail(T5AISleepFault_Controller);
    return false;
  }
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY) ||          \
    defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
  if (t5ai_native_alarm_cp_query(T5AIAlarm_ClockCount, &s_cp_clock_start) != S_SUCCESS) {
    prv_fail(T5AISleepFault_Controller);
    return false;
  }
#endif
  s_initialized = true;
  PBL_LOG_DBG("T5SLEEP contract state=%lx", (unsigned long)cp_state);
  return true;
}

#ifndef CONFIG_T5AI_RETAINED_SLEEP_TEST
static void prv_preflight_worker(void *context) {
  (void)context;
  (void)prv_initialize_contract(true);
}
#endif

static void prv_worker(void *context) {
  (void)context;
  pbl_irq_lock();
  const bool cleanup = s_phase == SleepCleanupQueued;
  const bool prepare = s_phase == SleepPrepareQueued;
  pbl_irq_unlock();
  if (!cleanup && !prepare)
    return;
  if (cleanup) {
    const status_t cancelled = t5ai_native_alarm_cancel(s_generation);
    if (cancelled != S_SUCCESS || !prv_canary_valid()) {
      prv_fail(T5AISleepFault_Cleanup);
      return;
    }
    uint32_t cp_entries;
    if (t5ai_native_alarm_cp_query(T5AIAlarm_LowVoltageCount, &cp_entries) != S_SUCCESS) {
      prv_fail(T5AISleepFault_Controller);
      return;
    }
    s_stats.cp_entries = cp_entries - s_cp_entries_start;
#if defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
    uint32_t mask_barriers;
    if (t5ai_native_alarm_cp_query(T5AIAlarm_MaskBarrierCount, &mask_barriers) != S_SUCCESS) {
      prv_fail(T5AISleepFault_Controller);
      return;
    }
    s_stats.cp_mask_barriers = mask_barriers - s_cp_mask_barriers_start;
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY) ||          \
    defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
    uint32_t clock_cycles;
    if (t5ai_native_alarm_cp_query(T5AIAlarm_ClockCount, &clock_cycles) != S_SUCCESS) {
      prv_fail(T5AISleepFault_Controller);
      return;
    }
    s_stats.cp_clock_cycles = clock_cycles - s_cp_clock_start;
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY) || \
    defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY)
    uint32_t resources;
    if (t5ai_native_alarm_cp_query(T5AIAlarm_ResourceCount, &resources) != S_SUCCESS) {
      prv_fail(T5AISleepFault_Controller);
      return;
    }
    s_stats.cp_resources = resources - s_cp_resources_start;
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY
    if (s_stats.cp_entries || s_stats.cp_resources || s_stats.cp_clock_cycles ||
        s_stats.cp_mask_barriers) {
      prv_fail(T5AISleepFault_Contract);
      return;
    }
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
    // Avoid mailbox traffic from per-entry logs before the next CP idle attempt.
    const bool report = prv_test_returns_complete() ||
                        s_stats.requests >= SLEEP_TEST_REQUEST_LIMIT || s_stats.fault;
    const bool report_hal =
        prv_test_returns_complete() || s_stats.requests >= SLEEP_TEST_REQUEST_LIMIT;
    uint32_t reject = 0u, pending_low = 0u, pending_high = 0u;
    uint32_t status_low = 0u, status_high = 0u, preparation = 0u, cp_ticks = 0u, stage = 0u;
    if (report) {
      uint32_t samples;
      if (t5ai_native_alarm_cp_query(T5AIAlarm_ReadySamples, &samples) != S_SUCCESS ||
          t5ai_native_alarm_cp_query(T5AIAlarm_MissingVotesLow, &s_stats.missing_low) !=
              S_SUCCESS ||
          t5ai_native_alarm_cp_query(T5AIAlarm_MissingVotesHigh, &s_stats.missing_high) !=
              S_SUCCESS) {
        prv_fail(T5AISleepFault_Controller);
        return;
      }
      s_stats.cp_samples = samples - s_cp_samples_start;
      if (report_hal) {
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_HalReject, &reject);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_HalPendingLow, &pending_low);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_HalPendingHigh, &pending_high);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_HalStatusLow, &status_low);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_HalStatusHigh, &status_high);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_PrepareTicks, &preparation);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_LowVoltageTicks, &cp_ticks);
        (void)t5ai_native_alarm_cp_query(T5AIAlarm_Stage, &stage);
      }
    }
    if (prv_test_returns_complete()) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY
      if (s_stats.cp_entries || s_stats.cp_resources || s_stats.cp_clock_cycles ||
          s_stats.cp_mask_barriers) {
        prv_fail(T5AISleepFault_Contract);
        return;
      }
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY)
      if (s_stats.cp_entries || !s_stats.cp_resources) {
        prv_fail(T5AISleepFault_Contract);
        return;
      }
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL)
      uint32_t mask_before = 0u, mask_after = 0u;
      if (s_stats.cp_entries || s_stats.cp_mask_barriers != 1u || !s_stats.cp_clock_cycles ||
          t5ai_native_alarm_cp_query(T5AIAlarm_MaskBefore, &mask_before) != S_SUCCESS ||
          t5ai_native_alarm_cp_query(T5AIAlarm_MaskAfter, &mask_after) != S_SUCCESS ||
          mask_before != mask_after) {
        prv_fail(T5AISleepFault_Contract);
        return;
      }
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY)
      uint32_t mode_low = 0u, hf_low = 0u, mask_before = 0u, mask_after = 0u;
      bool expected_dpll_on = false;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF
      expected_dpll_on = true;
#endif
      if (s_stats.cp_entries || !s_stats.cp_clock_cycles ||
          t5ai_native_alarm_cp_query(T5AIAlarm_ClockModeLow, &mode_low) != S_SUCCESS ||
          t5ai_native_alarm_cp_query(T5AIAlarm_HighFrequencyLow, &hf_low) != S_SUCCESS ||
          t5ai_native_alarm_cp_query(T5AIAlarm_MaskBefore, &mask_before) != S_SUCCESS ||
          t5ai_native_alarm_cp_query(T5AIAlarm_MaskAfter, &mask_after) != S_SUCCESS ||
          mask_before != mask_after || (mode_low & SLEEP_CORE_BUS_CLOCK_MASK) ||
          ((hf_low & SLEEP_DPLL_ENABLE) != 0u) != expected_dpll_on) {
        prv_fail(T5AISleepFault_Contract);
        return;
      }
#else
      if (!s_stats.cp_entries) {
        prv_fail(T5AISleepFault_NoControllerSleep);
        return;
      }
#endif
      if (t5ai_native_alarm_finish_cleanup(s_generation) != S_SUCCESS) {
        prv_fail(T5AISleepFault_Cleanup);
        return;
      }
      // Routine logging uses the same CP transport; finish the service lease first.
      PBL_LOG_DBG("T5SLEEP votes samples=%lu missing=%lx:%lx", (unsigned long)s_stats.cp_samples,
                  (unsigned long)s_stats.missing_high, (unsigned long)s_stats.missing_low);
      PBL_LOG_DBG("T5SLEEP check entries=%lu cp=%lu aborted=%lu late=%lu fault=%lu reject=%lx",
                  (unsigned long)s_stats.entries, (unsigned long)s_stats.cp_entries,
                  (unsigned long)s_stats.aborted, (unsigned long)s_stats.late_aon,
                  (unsigned long)s_stats.fault, (unsigned long)s_stats.rejected);
      if (report_hal) {
        PBL_LOG_DBG("T5SLEEP HAL reject=%lx pending=%lx:%lx status=%lx:%lx", (unsigned long)reject,
                    (unsigned long)pending_high, (unsigned long)pending_low,
                    (unsigned long)status_high, (unsigned long)status_low);
        PBL_LOG_DBG("T5SLEEP CP preparation=%lu ticks=%lu stage=%lu APticks=%lu",
                    (unsigned long)preparation, (unsigned long)cp_ticks, (unsigned long)stage,
                    (unsigned long)s_stats.aon_ticks);
      }
      PBL_LOG_DBG("T5SLEEP validated clocks=%lu masks=%lu resources=%lu",
                  (unsigned long)s_stats.cp_clock_cycles, (unsigned long)s_stats.cp_mask_barriers,
                  (unsigned long)s_stats.cp_resources);
      SLEEP_MARK_STAGE(SLEEP_TRACE_COMPLETE);
      s_phase = SleepTestComplete;
      PBL_LOG_DBG("T5SLEEP bounded test complete");
      return;
    }
    if (s_stats.requests >= SLEEP_TEST_REQUEST_LIMIT) {
      prv_fail(T5AISleepFault_AdmissionBudget);
      return;
    }
#endif
    if (t5ai_native_alarm_finish_cleanup(s_generation) != S_SUCCESS) {
      prv_fail(T5AISleepFault_Cleanup);
      return;
    }
    if (!s_stats.fault)
      SLEEP_MARK_STAGE(SLEEP_TRACE_COMPLETE);
#if defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY) && !defined(CONFIG_T5AI_RETAINED_SLEEP_TEST)
    if (!s_ap_only_reported && s_stats.entries >= SLEEP_AP_ONLY_REPORT_RETURNS) {
      s_ap_only_reported = true;
      PBL_LOG_DBG(
          "T5SLEEP AP-only active entries=%lu cp=%lu clocks=%lu masks=%lu resources=%lu fault=%lu",
          (unsigned long)s_stats.entries, (unsigned long)s_stats.cp_entries,
          (unsigned long)s_stats.cp_clock_cycles, (unsigned long)s_stats.cp_mask_barriers,
          (unsigned long)s_stats.cp_resources, (unsigned long)s_stats.fault);
    }
#endif
    s_phase = s_stats.fault ? SleepFailed : SleepIdle;
    return;
  }
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (!prv_test_window_open()) {
    s_phase = SleepIdle;
    return;
  }
#endif
  if (!prv_initialize_contract(false))
    return;
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  const int32_t remaining = (int32_t)(s_target - now);
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (remaining > 0 && (uint32_t)remaining > s_worker_budget_max_aon)
    s_worker_budget_max_aon = (uint32_t)remaining;
  if (remaining < (int32_t)(SLEEP_MIN_WORKER_WINDOW_MS * SLEEP_AON_PER_MS))
    ++s_worker_budget_refusals;
#endif
  const T5AISleepBlockers before = t5ai_sleep_blockers();
  if (before.faulted || before.owners ||
      remaining < (int32_t)(SLEEP_MIN_WORKER_WINDOW_MS * SLEEP_AON_PER_MS)) {
    if (before.faulted)
      prv_fail(T5AISleepFault_Owner);
    else
      s_phase = SleepIdle;
    return;
  }
  const uint32_t period =
      (uint32_t)remaining - (SLEEP_WAKE_MARGIN_MS + SLEEP_PREPARE_BUDGET_MS) * SLEEP_AON_PER_MS;
  prv_canary_prepare(s_stats.requests + 1u);
  s_earliest_wake = T5AI_AON_RTC_COUNT_LOW + period;
  const status_t status = t5ai_native_alarm_prepare_sleep(period, &s_generation);
  s_latest_wake = T5AI_AON_RTC_COUNT_LOW + period;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  const uint32_t request_latency = s_latest_wake - s_earliest_wake;
  if (request_latency > s_request_latency_max_aon)
    s_request_latency_max_aon = request_latency;
#endif
  ++s_stats.requests;
  if (status != S_SUCCESS) {
    prv_fail(T5AISleepFault_Controller);
    return;
  }
  pbl_irq_lock();
  // The applied reply can precede its final physical ACK/RX completion.
  s_phase = SleepArmed;
  ++s_stats.prepared;
  pbl_irq_unlock();
}

static void prv_recovery_diagnostic(void *context) {
  (void)context;
  uint32_t stage = 0u, generation = 0u;
  status_t status = t5ai_native_alarm_init();
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_PreviousStage, &stage);
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_PreviousGeneration, &generation);
  PBL_LOG_DBG("T5SLEEP recovery CP stage=%lu gen=%lu status=%ld", (unsigned long)stage,
              (unsigned long)generation, (long)status);
  uint32_t valid = 0u, wake = 0u, reset = 0u, power = 0u, config = 0u;
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_BootPMUSnapshotValid, &valid);
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_BootPMUWake, &wake);
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_BootPMUReset, &reset);
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_BootPMUPower, &power);
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_BootPMUWakeConfig, &config);
  PBL_LOG_DBG("T5SLEEP boot PMU valid=%lu wake=%lx reset=%lx power=%lx config=%lx status=%ld",
              (unsigned long)valid, (unsigned long)wake, (unsigned long)reset, (unsigned long)power,
              (unsigned long)config, (long)status);
  uint32_t analog = 0u;
  if (status == S_SUCCESS)
    status = t5ai_native_alarm_cp_query(T5AIAlarm_AnalogWakeState, &analog);
  PBL_LOG_DBG("T5SLEEP analog wake state=%lx status=%ld", (unsigned long)analog, (long)status);
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (!t5ai_mailbox_channel_test_timeout_output()) {
    prv_fail(T5AISleepFault_Controller);
    return;
  }
#endif
  if (s_phase != SleepFailed)
    (void)prv_initialize_contract(true);
}

void t5ai_sleep_start(void) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY
  // Board calls this after validating its AP-owned PSRAM ranges.
  t5ai_sleep_profile_timers_ready();
#endif
  if (s_registered)
    return;
  s_registered = true;
#if defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY) && !defined(CONFIG_T5AI_RETAINED_SLEEP_TEST)
  s_ap_only_started = T5AI_AON_RTC_COUNT_LOW;
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  t5ai_mailbox_channel_dump_previous_timeout();
#endif
  if (s_sleep_trace.magic == T5AI_SLEEP_TRACE_MAGIC &&
      s_sleep_trace.stage == ~s_sleep_trace.inverse)
    PBL_LOG_DBG("T5SLEEP previous AP stage=%lu gen=%lu", (unsigned long)s_sleep_trace.stage,
                (unsigned long)s_sleep_trace.generation);
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  const uint8_t cp_reason = t5ai_reset_reason_get_boot_cp();
  const bool durable = cp_reason != UINT8_MAX && (cp_reason & T5AI_SLEEP_CP_TRACE_REASON_MASK) ==
                                                     T5AI_SLEEP_CP_TRACE_REASON_BASE;
  PBL_LOG_DBG("T5SLEEP durable boot CP=%x stage=%u valid=%u AP=%x", cp_reason,
              durable ? cp_reason & T5AI_SLEEP_CP_TRACE_STAGE_MASK : 0u, durable,
              t5ai_reset_reason_get_boot_ap());
#endif
  const bool unfinished = s_sleep_trace.magic == T5AI_SLEEP_TRACE_MAGIC &&
                          s_sleep_trace.stage == ~s_sleep_trace.inverse &&
                          s_sleep_trace.stage != SLEEP_TRACE_COMPLETE;
  if (t5ai_reset_reason_is_sleep_timeout(t5ai_reset_reason_get_boot_ap()) || unfinished) {
    prv_fail(T5AISleepFault_ResumeTimeout);
    if (!pbl_sys_work_try_call(prv_recovery_diagnostic, NULL))
      PBL_LOG_ERR("T5SLEEP recovery diagnostic dropped");
    return;
  }
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  // A global reset can discard the AP reason and breadcrumb while CP trace survives.
  if (!pbl_sys_work_try_call(prv_recovery_diagnostic, NULL))
    PBL_LOG_ERR("T5SLEEP startup diagnostic dropped");
  s_test_not_before = T5AI_AON_RTC_COUNT_LOW + SLEEP_START_DELAY_MS * SLEEP_AON_PER_MS;
#endif
  s_phase = SleepIdle;
#ifndef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (!pbl_sys_work_try_call(prv_preflight_worker, NULL))
    PBL_LOG_DBG("T5SLEEP contract preflight deferred");
#endif
}

void t5ai_sleep_service(uint32_t deadline) {
  if (!s_registered)
    return;
#if defined(CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY) && !defined(CONFIG_T5AI_RETAINED_SLEEP_TEST)
  // Preserve the bounded control's startup settling period before continuous entry.
  if (!s_ap_only_settled) {
    if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - s_ap_only_started) <
        SLEEP_START_DELAY_MS * SLEEP_AON_PER_MS)
      return;
    s_ap_only_settled = true;
  }
#endif
  if (s_phase == SleepAwaitController) {
    if (!s_controller_wake_queued) {
      // Drain a frame whose IRQ edge may have arrived while AP was masked.
      t5ai_mailbox_channel_wake();
      s_controller_wake_queued = true;
    }
    if (t5ai_native_alarm_event_received(s_generation)) {
      s_phase = SleepNeedsCleanup;
    } else if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - s_controller_wait_start) >=
               SLEEP_CP_EVENT_TIMEOUT_MS * SLEEP_AON_PER_MS) {
      // Idle context cannot enter the logger or issue a CP cleanup request.
      s_stats.fault = T5AISleepFault_Controller;
      s_phase = SleepFailed;
      t5ai_sleep_fault();
      t5ai_force_whole_chip_reset(T5AI_RESET_REASON_SLEEP_WAIT_BASE);
      return;
    } else {
      return;
    }
  }
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (!prv_test_window_open()) {
    // Complete an existing lease, but never prepare another outside this slot.
    if (s_phase == SleepArmed || s_phase == SleepPrepared)
      s_phase = SleepNeedsCleanup;
    if (s_phase != SleepNeedsCleanup) {
      const time_t slot = CONFIG_T5AI_RETAINED_SLEEP_TEST_AT_UTC;
      const time_t now = pbl_realtime();
      if (slot && now >= slot && (uint32_t)(now - slot) >= SLEEP_TEST_SLOT_SECONDS &&
          (s_phase == SleepIdle || s_phase == SleepTestComplete || s_phase == SleepFailed) &&
          !s_test_reported && !s_test_report_queued) {
        s_test_report_queued = true;
        if (!pbl_sys_work_try_call(prv_test_report, NULL))
          s_test_report_queued = false;
      }
      return;
    }
  }
#endif
  bool queue = false;
  pbl_irq_lock();
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  const int32_t idle_budget = (int32_t)(deadline - now);
  if (idle_budget > 0 && (uint32_t)idle_budget > s_idle_budget_max_aon)
    s_idle_budget_max_aon = (uint32_t)idle_budget;
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  const uint32_t delay_ticks = SLEEP_START_DELAY_MS * SLEEP_AON_PER_MS;
  if ((uint32_t)(now - (s_test_not_before - delay_ticks)) < delay_ticks) {
    pbl_irq_unlock();
    return;
  }
#endif
#ifdef CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY
  pbl_irq_unlock();
  prv_budget_observe(deadline);
  return;
#endif
  if (s_phase == SleepNeedsCleanup ||
      ((s_phase == SleepArmed || s_phase == SleepPrepared) &&
       (t5ai_sleep_blockers().faulted || t5ai_native_alarm_woken(s_generation) ||
        (s_phase == SleepPrepared && !t5ai_sleep_blockers_match(s_epoch)) ||
        (int32_t)(deadline - s_latest_wake) < (int32_t)(SLEEP_WAKE_MARGIN_MS * SLEEP_AON_PER_MS) ||
        (int32_t)(s_earliest_wake - now) <= (int32_t)(SLEEP_ENTRY_LEAD_MS * SLEEP_AON_PER_MS)))) {
    if (s_phase == SleepArmed || s_phase == SleepPrepared)
      ++s_stats.aborted;
    s_phase = SleepCleanupQueued;
    queue = true;
  } else if (s_phase == SleepArmed) {
    const T5AISleepBlockers quiet = t5ai_sleep_blockers();
    if (!quiet.owners && t5ai_mailbox_channel_sleep_ready() && t5ai_mb_ipc_sleep_ready() &&
        t5ai_hci_sleep_ready()) {
      s_epoch = quiet.epoch;
      s_phase = SleepPrepared;
    }
  } else if (s_phase == SleepIdle &&
             (int32_t)(deadline - now) >= (int32_t)(SLEEP_MIN_WINDOW_MS * SLEEP_AON_PER_MS)) {
    const uint32_t maximum = now + SLEEP_MAX_WINDOW_MS * SLEEP_AON_PER_MS;
    s_target = (int32_t)(deadline - maximum) > 0 ? maximum : deadline;
    s_phase = SleepPrepareQueued;
    queue = true;
  }
  pbl_irq_unlock();
  if (queue && !pbl_sys_work_try_call(prv_worker, NULL)) {
    pbl_irq_lock();
    s_phase = s_phase == SleepCleanupQueued ? SleepNeedsCleanup : SleepIdle;
    ++s_stats.dropped;
    pbl_irq_unlock();
  }
}

bool t5ai_sleep_enter(uint32_t deadline) {
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  if (s_phase != SleepPrepared)
    return false;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  if (!prv_test_window_open()) {
    s_phase = SleepNeedsCleanup;
    return false;
  }
#endif
  enum {
    RejectEpoch = 1u << 0,
    RejectPhysical = 1u << 1,
    RejectLogical = 1u << 2,
    RejectHci = 1u << 3,
    RejectWake = 1u << 4,
    RejectController = 1u << 5,
    RejectClock = 1u << 6,
    RejectLead = 1u << 7,
    RejectDeadline = 1u << 8,
    RejectDeep = 1u << 9,
    RejectTick = 1u << 10,
  };
  uint32_t rejected = 0u;
  if (!t5ai_sleep_blockers_match(s_epoch))
    rejected |= RejectEpoch;
  if (!t5ai_mailbox_channel_sleep_ready())
    rejected |= RejectPhysical;
  if (!t5ai_mb_ipc_sleep_ready())
    rejected |= RejectLogical;
  if (!t5ai_hci_sleep_ready())
    rejected |= RejectHci;
  if (t5ai_native_alarm_woken(s_generation))
    rejected |= RejectWake;
  if (!(SLEEP_MMIO(SLEEP_AON_COORDINATION_ADDRESS) & SLEEP_CP_READY))
    rejected |= RejectController;
  if (!prv_clock_ready(SLEEP_MMIO(SLEEP_CORE_CLOCK_MODE_ADDRESS),
                       SLEEP_MMIO(SLEEP_AP_CLOCK_CONTROL_ADDRESS)))
    rejected |= RejectClock;
  if ((int32_t)(s_earliest_wake - now) <= (int32_t)(SLEEP_ENTRY_LEAD_MS * SLEEP_AON_PER_MS))
    rejected |= RejectLead;
  if ((int32_t)(deadline - s_latest_wake) < (int32_t)(SLEEP_WAKE_MARGIN_MS * SLEEP_AON_PER_MS))
    rejected |= RejectDeadline;
  if (SCB->SCR & SCB_SCR_SLEEPDEEP_Msk)
    rejected |= RejectDeep;
  if (!rejected && !t5ai_tick_prepare_retained())
    rejected |= RejectTick;
  if (rejected) {
    s_stats.rejected = rejected;
    return false;
  }
  if (!t5ai_watchdog_sleep_guard_arm()) {
    t5ai_tick_resume_idle();
    s_phase = SleepNeedsCleanup;
    s_stats.fault = T5AISleepFault_WatchdogGuard;
    t5ai_sleep_fault();
    return false;
  }
  s_sleep_trace.magic = 0u;
  s_sleep_trace.generation = s_generation;
  s_phase = SleepNeedsCleanup;
  const bool slept = prv_retained_wfi();
  t5ai_watchdog_sleep_guard_restore();
  if (!slept) {
    ++s_stats.aborted;
    t5ai_tick_resume_idle();
    return false;
  }
  const uint32_t end = T5AI_AON_RTC_COUNT_LOW;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
  const uint32_t duration = end - now;
  if (duration > s_suspend_duration_max_aon)
    s_suspend_duration_max_aon = duration;
  const uint32_t headroom = s_earliest_wake - now;
  if (headroom < s_entry_headroom_min_aon)
    s_entry_headroom_min_aon = headroom;
#endif
  if ((int32_t)(end - deadline) > 0) {
    s_stats.late_aon = end - deadline;
    s_stats.fault = T5AISleepFault_Deadline;
  }
  ++s_stats.entries;
  s_stats.aon_ticks += end - now;
  // Restored clocks permit execution; CP mailbox service needs its deferred event.
  s_controller_wait_start = end;
  s_controller_wake_queued = false;
  s_phase = SleepAwaitController;
  t5ai_tick_resume_idle();
  if (s_stats.fault)
    t5ai_sleep_fault();
  return true;
}

void t5ai_sleep_stats(T5AISleepStats *stats) {
  pbl_irq_lock();
  *stats = s_stats;
  pbl_irq_unlock();
}
