/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/watchdog.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/state_trace.h>

#include <cmsis_core.h>

#include <stdint.h>

// Force-reboot values reproduce wdt_hal_force_reboot(), including both key writes.
#define T5AI_WDT_CLOCK_SOURCE_MASK 0x3u
#define T5AI_WDT_RESET_CONTROL_MASK 0x3fu
#define T5AI_WDT_WHOLE_CHIP_RESET_CONTROL 0x26u
#define T5AI_NMI_WDT_CLOCK_GATE_BYPASS (1u << 1)
#define T5AI_WDT_FORCE_RESET_PERIOD 10u

/* BK7258 AP register addresses from the pinned T5AI BSP. */
#define T5AI_AON_PMU_R0 (*(volatile uint32_t *)0x44000000u)
#define T5AI_AON_WDT_CTRL (*(volatile uint32_t *)0x44000600u)
#define T5AI_AON_PMU_R2 (*(volatile uint32_t *)0x44000008u)
#define T5AI_AON_PMU_R41 (*(volatile uint32_t *)0x44000104u)
#define T5AI_NMI_WDT_CTRL (*(volatile uint32_t *)0x44800008u)
#define T5AI_NMI_WDT_PERIOD (*(volatile uint32_t *)0x44800010u)

#define T5AI_AON_WDT_KEY_1 0x5au
#define T5AI_AON_WDT_KEY_2 0xa5u
#define T5AI_AON_WDT_KEY_SHIFT 16u
#define T5AI_AON_WDT_PERIOD_MS 8000u

#define T5AI_AP_RESET_REASON_SHIFT 24u
#define T5AI_AP_RESET_REASON_MASK (0x7fu << T5AI_AP_RESET_REASON_SHIFT)
#define T5AI_RESET_SOURCE_WATCHDOG 0x02u

static bool s_started;
static bool s_initialized;
static McuRebootReason s_reset_reason;

static void prv_set_period(uint16_t period_ms) {
  T5AI_AON_WDT_CTRL =
      (T5AI_AON_WDT_KEY_1 << T5AI_AON_WDT_KEY_SHIFT) | period_ms;
  T5AI_AON_WDT_CTRL =
      (T5AI_AON_WDT_KEY_2 << T5AI_AON_WDT_KEY_SHIFT) | period_ms;
}

void t5ai_reset_reason_set_ap(uint8_t reason) {
  uint32_t value = T5AI_AON_PMU_R0;
  value &= ~T5AI_AP_RESET_REASON_MASK;
  value |= ((uint32_t)reason << T5AI_AP_RESET_REASON_SHIFT) &
           T5AI_AP_RESET_REASON_MASK;
  T5AI_AON_PMU_R0 = value;
}

uint8_t t5ai_reset_reason_get_ap(void) {
  return (uint8_t)((T5AI_AON_PMU_R0 & T5AI_AP_RESET_REASON_MASK) >>
                   T5AI_AP_RESET_REASON_SHIFT);
}

void watchdog_init(void) {
  if (s_initialized) {
    return;
  }
  s_started = false;
  s_reset_reason = (McuRebootReason) { 0 };
  const uint32_t reason = t5ai_reset_reason_get_ap();
  s_reset_reason.independent_watchdog_reset =
      reason == T5AI_RESET_SOURCE_WATCHDOG;
  s_initialized = true;
}

void watchdog_start(void) {
  s_started = true;
  t5ai_reset_reason_set_ap(T5AI_RESET_SOURCE_WATCHDOG);
  prv_set_period(T5AI_AON_WDT_PERIOD_MS);
}

void watchdog_stop(void) {
  prv_set_period(0);
  s_started = false;
}

void watchdog_feed(void) {
  if (s_started) {
    prv_set_period(T5AI_AON_WDT_PERIOD_MS);
  }
}

bool watchdog_check_reset_flag(void) {
  watchdog_init();
  return s_reset_reason.independent_watchdog_reset;
}

McuRebootReason watchdog_clear_reset_flag(void) {
  watchdog_init();
  const McuRebootReason reason = s_reset_reason;
  s_reset_reason = (McuRebootReason) { 0 };
  if (reason.independent_watchdog_reset) {
    t5ai_reset_reason_set_ap(0);
  }
  return reason;
}

McuRebootReason watchdog_get_reset_flag(void) {
  watchdog_init();
  return s_reset_reason;
}

PBL_NORETURN void t5ai_force_whole_chip_reset(uint8_t reason) {
  t5ai_state_trace_freeze(reason);
  __disable_irq();

  t5ai_reset_reason_set_ap(reason);

  /* This is the pinned BK7258 wdt_hal_force_reboot sequence. Selecting DIVD
   * prevents a stopped ROSC from defeating recovery, and 0x26 makes watchdog
   * expiry reset the complete device instead of CPU1 alone. */
  T5AI_AON_PMU_R41 &= ~T5AI_WDT_CLOCK_SOURCE_MASK;
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
