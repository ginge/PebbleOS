/* SPDX-License-Identifier: Apache-2.0 */
#include "console/prompt.h"
#include "pbl/drivers/task_watchdog.h"
#include "pbl/soc/t5ai/freertos.h"
#include "pbl/soc/t5ai/power.h"
#include "pbl/drivers/rtc.h"
#include "pbl/services/analytics/analytics.h"

#include "FreeRTOS.h"
#include "task.h"
#include <cmsis_core.h>

#include <inttypes.h>
#include <stdio.h>

static RtcTicks s_wfi_ticks;
static RtcTicks s_last_stats_ticks;

static uint32_t prv_percentage(RtcTicks value, RtcTicks total) {
  if (total == 0) {
    return 0;
  }
  return (uint32_t)((value * 10000u) / total);
}

void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks) {
  (void)expected_idle_ticks;

  const RtcTicks start = rtc_get_ticks();
  __DSB();
  __WFI();
  __ISB();
  s_wfi_ticks += rtc_get_ticks() - start;
}

void vApplicationTickHook(void) {
  static TickType_t watchdog_ticks;

  if (++watchdog_ticks >= pdMS_TO_TICKS(TASK_WATCHDOG_FEED_PERIOD_MS)) {
    watchdog_ticks = 0;
    task_watchdog_feed();
  }
}

bool vPortEnableTimer(void) {
  return false;
}

void t5ai_freertos_apply_systick_clock(void) {
  // Elect the active clock once and use its actual hardware rate. Idle WFI
  // leaves that clock unchanged, matching Round 2's active/scheduler model.
  t5ai_power_init();
  PBL_ASSERTN((SystemCoreClock % RTC_TICKS_HZ) == 0u);
  const uint32_t reload = SystemCoreClock / RTC_TICKS_HZ - 1u;
  PBL_ASSERTN(reload <= SysTick_LOAD_RELOAD_Msk);
  SysTick->LOAD = reload;
}

void dump_current_runtime_stats(void) {
  const RtcTicks now = rtc_get_ticks();
  const RtcTicks total = now - s_last_stats_ticks;
  const RtcTicks sleep = s_wfi_ticks < total ? s_wfi_ticks : total;
  const RtcTicks running = total - sleep;
  char buffer[96];

  snprintf(buffer, sizeof(buffer), "Run:   %" PRIu64 " ticks (%" PRIu32 ".%02" PRIu32 " %%)",
           running, prv_percentage(running, total) / 100u, prv_percentage(running, total) % 100u);
  prompt_send_response(buffer);
  snprintf(buffer, sizeof(buffer), "WFI:   %" PRIu64 " ticks (%" PRIu32 ".%02" PRIu32 " %%)", sleep,
           prv_percentage(sleep, total) / 100u, prv_percentage(sleep, total) % 100u);
  prompt_send_response(buffer);
  snprintf(buffer, sizeof(buffer), "Total: %" PRIu64 " ticks", total);
  prompt_send_response(buffer);
}

void pbl_analytics_external_collect_cpu_stats(void) {
  const RtcTicks now = rtc_get_ticks();
  const RtcTicks total = now - s_last_stats_ticks;
  const RtcTicks sleep = s_wfi_ticks < total ? s_wfi_ticks : total;
  const RtcTicks running = total - sleep;

  PBL_ANALYTICS_SET_UNSIGNED(cpu_running_pct, prv_percentage(running, total));
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep0_pct, prv_percentage(sleep, total));
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep1_pct, 0);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep2_pct, 0);

  s_last_stats_ticks = now;
  s_wfi_ticks = 0;
}
