/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/reboot_reason.h"

#include <stdint.h>

#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/section.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/state_trace.h>

// Force-reboot values reproduce wdt_hal_force_reboot(), including both key writes.
#define T5AI_AON_LPO_SOURCE_MASK          0x3u
#define T5AI_WDT_RESET_CONTROL_MASK       0x3fu
#define T5AI_WDT_WHOLE_CHIP_RESET_CONTROL 0x26u
#define T5AI_NMI_WDT_CLOCK_GATE_BYPASS    (1u << 1)
#define T5AI_WDT_FORCE_RESET_PERIOD       10u

/* BK7258 AP register addresses from the pinned T5AI BSP. */
// CP decodes these shared words before clearing the live PMU fields.
#define T5AI_CP_SAVED_RESET_REASON (*(volatile uint32_t *)0x2809f718u)
#define T5AI_AP_SAVED_RESET_REASON (*(volatile uint32_t *)0x2809f71cu)
#define T5AI_AON_PMU_R0            (*(volatile uint32_t *)0x44000000u)
#define T5AI_AON_WDT_CTRL          (*(volatile uint32_t *)0x44000600u)
#define T5AI_AON_PMU_R25           (*(volatile uint32_t *)0x44000094u)
#define T5AI_AON_PMU_R2            (*(volatile uint32_t *)0x44000008u)
#define T5AI_AON_PMU_R41           (*(volatile uint32_t *)0x44000104u)
#define T5AI_NMI_WDT_CTRL          (*(volatile uint32_t *)0x44800008u)
#define T5AI_NMI_WDT_PERIOD        (*(volatile uint32_t *)0x44800010u)

#define T5AI_AON_WDT_KEY_1     0x5au
#define T5AI_AON_WDT_KEY_2     0xa5u
#define T5AI_AON_WDT_KEY_SHIFT 16u
// The AON period is milliseconds in a 16-bit field; zero disables it.
_Static_assert(CONFIG_WATCHDOG_TIMEOUT_MS > 0 && CONFIG_WATCHDOG_TIMEOUT_MS <= UINT16_MAX,
               "T5AI watchdog timeout must fit the nonzero 16-bit AON period");

#define T5AI_AP_RESET_REASON_SHIFT 24u
#define T5AI_AP_RESET_REASON_MASK  (0x7fu << T5AI_AP_RESET_REASON_SHIFT)
#define T5AI_RESET_SOURCE_WATCHDOG 0x02u

static bool s_boot_captured;
static uint8_t s_boot_ap, s_boot_cp;
static bool s_started;
static bool s_initialized;
static McuRebootReason s_reset_reason;

static void prv_set_period(uint16_t period_ms) {
  T5AI_AON_WDT_CTRL = (T5AI_AON_WDT_KEY_1 << T5AI_AON_WDT_KEY_SHIFT) | period_ms;
  T5AI_AON_WDT_CTRL = (T5AI_AON_WDT_KEY_2 << T5AI_AON_WDT_KEY_SHIFT) | period_ms;
}

#define T5AI_RESET_REASON_COMMIT_KEY_1 0x424b55aau
#define T5AI_RESET_REASON_COMMIT_KEY_2 0xbdb4aa55u

// Match the pinned AP/CP aon_pmu_hal_set_r0 sequence, including its latch keys.
static inline __attribute__((always_inline)) void prv_commit_reset_reason(void) {
  T5AI_AON_PMU_R25 = T5AI_RESET_REASON_COMMIT_KEY_1;
  T5AI_AON_PMU_R25 = T5AI_RESET_REASON_COMMIT_KEY_2;
  __DSB();
}

void t5ai_reset_reason_set_ap(uint8_t reason) {
  uint32_t value = T5AI_AON_PMU_R0;
  value &= ~T5AI_AP_RESET_REASON_MASK;
  value |= ((uint32_t)reason << T5AI_AP_RESET_REASON_SHIFT) & T5AI_AP_RESET_REASON_MASK;
  T5AI_AON_PMU_R0 = value;
  prv_commit_reset_reason();
}

uint8_t t5ai_reset_reason_get_ap(void) {
  return (uint8_t)((T5AI_AON_PMU_R0 & T5AI_AP_RESET_REASON_MASK) >> T5AI_AP_RESET_REASON_SHIFT);
}

void t5ai_reset_reason_capture_boot(void) {
  if (s_boot_captured)
    return;
  s_boot_ap = (uint8_t)(T5AI_AP_SAVED_RESET_REASON & 0x7fu);
  s_boot_cp = (uint8_t)(T5AI_CP_SAVED_RESET_REASON & 0xffu);
  s_boot_captured = true;
}

uint8_t t5ai_reset_reason_get_boot_ap(void) {
  t5ai_reset_reason_capture_boot();
  return s_boot_ap;
}

uint8_t t5ai_reset_reason_get_boot_cp(void) {
  t5ai_reset_reason_capture_boot();
  return s_boot_cp;
}

void watchdog_init(void) {
  if (s_initialized) {
    return;
  }
  s_started = false;
  s_reset_reason = (McuRebootReason){0};
  const uint32_t reason = t5ai_reset_reason_get_boot_ap();
  s_reset_reason.independent_watchdog_reset = reason == T5AI_RESET_SOURCE_WATCHDOG;
  s_initialized = true;
}

void watchdog_start(void) {
  s_started = true;
  t5ai_reset_reason_set_ap(T5AI_RESET_SOURCE_WATCHDOG);
  prv_set_period(CONFIG_WATCHDOG_TIMEOUT_MS);
  t5ai_reboot_trace_watchdog(true);
}

void watchdog_stop(void) {
  prv_set_period(0);
  s_started = false;
  t5ai_reboot_trace_watchdog(false);
}

void watchdog_feed(void) {
  if (s_started) {
    prv_set_period(CONFIG_WATCHDOG_TIMEOUT_MS);
    t5ai_reboot_trace_watchdog(true);
  }
}

bool watchdog_check_reset_flag(void) {
  watchdog_init();
  return s_reset_reason.independent_watchdog_reset;
}

McuRebootReason watchdog_clear_reset_flag(void) {
  watchdog_init();
  const McuRebootReason reason = s_reset_reason;
  s_reset_reason = (McuRebootReason){0};
  if (reason.independent_watchdog_reset) {
    t5ai_reset_reason_set_ap(0);
  }
  return reason;
}

McuRebootReason watchdog_get_reset_flag(void) {
  watchdog_init();
  return s_reset_reason;
}

[[noreturn]] void t5ai_force_whole_chip_reset(uint8_t reason) {
  __disable_irq();
  if (reason == T5AI_RESET_REASON_PEBBLE_BASE) {
    const uint8_t existing = t5ai_reset_reason_get_ap();
    const bool mailbox =
        existing >= T5AI_RESET_REASON_MAILBOX_BASE && existing < T5AI_RESET_REASON_PEBBLE_BASE;
    const bool pebble = existing > T5AI_RESET_REASON_PEBBLE_BASE &&
                        existing <= T5AI_RESET_REASON_PEBBLE_BASE + RebootReasonCode_MailboxTimeout;
    if (mailbox || pebble)
      reason = existing;
  }
  t5ai_reboot_trace_event(T5RebootTrace_ForceReset, reason);
  t5ai_state_trace_freeze(reason);

  t5ai_force_whole_chip_reset_from_sram(reason);
}

[[noreturn]] PBL_SECTION_RAM void t5ai_force_whole_chip_reset_from_sram(uint8_t reason) {
  __disable_irq();
  // No flash, PSRAM, logging or cache calls are safe on a failed clock restore.
  T5AI_AON_PMU_R0 = (T5AI_AON_PMU_R0 & ~T5AI_AP_RESET_REASON_MASK) |
                    (((uint32_t)reason << T5AI_AP_RESET_REASON_SHIFT) & T5AI_AP_RESET_REASON_MASK);
  prv_commit_reset_reason();

  /* This is the pinned BK7258 wdt_hal_force_reboot sequence. Selecting DIVD
   * prevents a stopped ROSC from defeating recovery, and 0x26 makes watchdog
   * expiry reset the complete device instead of CPU1 alone. */
  T5AI_AON_PMU_R41 &= ~T5AI_AON_LPO_SOURCE_MASK;
  T5AI_AON_PMU_R2 =
      (T5AI_AON_PMU_R2 & ~T5AI_WDT_RESET_CONTROL_MASK) | T5AI_WDT_WHOLE_CHIP_RESET_CONTROL;
  T5AI_NMI_WDT_CTRL |= T5AI_NMI_WDT_CLOCK_GATE_BYPASS;
  T5AI_NMI_WDT_PERIOD =
      (T5AI_AON_WDT_KEY_1 << T5AI_AON_WDT_KEY_SHIFT) | T5AI_WDT_FORCE_RESET_PERIOD;
  T5AI_NMI_WDT_PERIOD =
      (T5AI_AON_WDT_KEY_2 << T5AI_AON_WDT_KEY_SHIFT) | T5AI_WDT_FORCE_RESET_PERIOD;
  T5AI_AON_WDT_CTRL = (T5AI_AON_WDT_KEY_1 << T5AI_AON_WDT_KEY_SHIFT) | T5AI_WDT_FORCE_RESET_PERIOD;
  T5AI_AON_WDT_CTRL = (T5AI_AON_WDT_KEY_2 << T5AI_AON_WDT_KEY_SHIFT) | T5AI_WDT_FORCE_RESET_PERIOD;
  __DSB();
  while (true) {
    __WFI();
  }
}

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
// Separate a lost-WFI guard from the shorter SRAM restore deadline in pilots.
#ifdef CONFIG_T5AI_RETAINED_SLEEP_TEST
#define T5AI_SLEEP_GUARD_PERIOD_MS 4000u
#else
#define T5AI_SLEEP_GUARD_PERIOD_MS 1000u
#endif
#define T5AI_LPO_SOURCE_EXTERNAL 1u
#define T5AI_LPO_SOURCE_DIVD     0u
#define T5AI_LPO_HALT            (1u << 24)
#define T5AI_AON_WDT_PERIOD_MASK UINT16_MAX
static bool s_sleep_guard;
static uint32_t s_guard_period, s_guard_reset_control;
static uint8_t s_guard_reason;

bool t5ai_watchdog_sleep_guard_arm(void) {
  const uint32_t lpo = T5AI_AON_PMU_R41;
  const uint32_t source = lpo & T5AI_AON_LPO_SOURCE_MASK;
  const bool clock_ready = source == T5AI_LPO_SOURCE_EXTERNAL || source == T5AI_LPO_SOURCE_DIVD;
  if (s_sleep_guard || !clock_ready || (lpo & T5AI_LPO_HALT))
    return false;
  s_guard_period = T5AI_AON_WDT_CTRL & T5AI_AON_WDT_PERIOD_MASK;
  s_guard_reset_control = T5AI_AON_PMU_R2 & T5AI_WDT_RESET_CONTROL_MASK;
  s_guard_reason = t5ai_reset_reason_get_ap();
  s_sleep_guard = true;
  t5ai_reset_reason_set_ap(T5AI_RESET_REASON_SLEEP_RESUME);
  T5AI_AON_PMU_R2 =
      (T5AI_AON_PMU_R2 & ~T5AI_WDT_RESET_CONTROL_MASK) | T5AI_WDT_WHOLE_CHIP_RESET_CONTROL;
  prv_set_period(T5AI_SLEEP_GUARD_PERIOD_MS);
  __DSB();
  const bool accepted =
      (T5AI_AON_WDT_CTRL & T5AI_AON_WDT_PERIOD_MASK) == T5AI_SLEEP_GUARD_PERIOD_MS &&
      (T5AI_AON_PMU_R2 & T5AI_WDT_RESET_CONTROL_MASK) == T5AI_WDT_WHOLE_CHIP_RESET_CONTROL &&
      t5ai_reset_reason_get_ap() == T5AI_RESET_REASON_SLEEP_RESUME &&
      (T5AI_AON_PMU_R41 & (T5AI_AON_LPO_SOURCE_MASK | T5AI_LPO_HALT)) == source;
  if (!accepted) {
    t5ai_watchdog_sleep_guard_restore();
    return false;
  }
  return true;
}

void t5ai_watchdog_sleep_guard_restore(void) {
  if (!s_sleep_guard)
    return;
  prv_set_period(s_guard_period);
  T5AI_AON_PMU_R2 = (T5AI_AON_PMU_R2 & ~T5AI_WDT_RESET_CONTROL_MASK) | s_guard_reset_control;
  t5ai_reset_reason_set_ap(s_guard_reason);
  s_sleep_guard = false;
}
#endif
