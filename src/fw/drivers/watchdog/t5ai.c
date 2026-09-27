/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/watchdog.h>
#include <pbl/soc/t5ai/reset.h>

#include <cmsis_core.h>

#include <stdint.h>

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
#define T5AI_AON_WDT_MAX_PERIOD_MS 0xffffu
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

static void prv_set_ap_reset_reason(uint32_t reason) {
  uint32_t value = T5AI_AON_PMU_R0;
  value &= ~T5AI_AP_RESET_REASON_MASK;
  value |= (reason << T5AI_AP_RESET_REASON_SHIFT) &
           T5AI_AP_RESET_REASON_MASK;
  T5AI_AON_PMU_R0 = value;
}

void watchdog_init(void) {
  if (s_initialized) {
    return;
  }
  s_started = false;
  s_reset_reason = (McuRebootReason) { 0 };
  const uint32_t reason =
      (T5AI_AON_PMU_R0 & T5AI_AP_RESET_REASON_MASK) >>
      T5AI_AP_RESET_REASON_SHIFT;
  s_reset_reason.independent_watchdog_reset =
      reason == T5AI_RESET_SOURCE_WATCHDOG;
  s_initialized = true;
}

void watchdog_start(void) {
  s_started = true;
  prv_set_ap_reset_reason(T5AI_RESET_SOURCE_WATCHDOG);
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
    prv_set_ap_reset_reason(0);
  }
  return reason;
}

McuRebootReason watchdog_get_reset_flag(void) {
  watchdog_init();
  return s_reset_reason;
}

NORETURN t5ai_force_whole_chip_reset(void) {
  __disable_irq();

  /* This is the pinned BK7258 wdt_hal_force_reboot sequence. Selecting DIVD
   * prevents a stopped ROSC from defeating recovery, and 0x26 makes watchdog
   * expiry reset the complete device instead of CPU1 alone. */
  T5AI_AON_PMU_R41 &= ~0x3u;
  T5AI_AON_PMU_R2 = (T5AI_AON_PMU_R2 & ~0x3fu) | 0x26u;
  T5AI_NMI_WDT_CTRL |= 1u << 1u;
  T5AI_NMI_WDT_PERIOD = 0x5a000au;
  T5AI_NMI_WDT_PERIOD = 0xa5000au;
  T5AI_AON_WDT_CTRL = 0x5a000au;
  T5AI_AON_WDT_CTRL = 0xa5000au;
  __DSB();
  while (true) {
    __WFI();
  }
}
