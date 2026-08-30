/* SPDX-License-Identifier: Apache-2.0 */
#include "console/prompt.h"
#include "pbl/drivers/rtc.h"
#include "pbl/services/analytics/analytics.h"

#include "FreeRTOS.h"
#include "task.h"
#include <cmsis_core.h>

#include <inttypes.h>
#include <stdio.h>

#define T5AI_SYSTICK_RELOAD \
  (CONFIG_T5AI_CORE_CLOCK_HZ / RTC_TICKS_HZ - 1u)

_Static_assert(CONFIG_T5AI_CORE_CLOCK_HZ % RTC_TICKS_HZ == 0u,
               "T5AI core clock must produce an exact FreeRTOS tick");
_Static_assert(T5AI_SYSTICK_RELOAD <= SysTick_LOAD_RELOAD_Msk,
               "T5AI FreeRTOS tick does not fit the SysTick reload register");

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

bool vPortEnableTimer(void) {
  return false;
}

void dump_current_runtime_stats(void) {
  const RtcTicks now = rtc_get_ticks();
  const RtcTicks total = now - s_last_stats_ticks;
  const RtcTicks sleep = s_wfi_ticks < total ? s_wfi_ticks : total;
  const RtcTicks running = total - sleep;
  char buffer[96];

  snprintf(buffer, sizeof(buffer), "Run:   %" PRIu64 " ticks (%" PRIu32 ".%02" PRIu32 " %%)",
           running, prv_percentage(running, total) / 100u,
           prv_percentage(running, total) % 100u);
  prompt_send_response(buffer);
  snprintf(buffer, sizeof(buffer), "WFI:   %" PRIu64 " ticks (%" PRIu32 ".%02" PRIu32 " %%)",
           sleep, prv_percentage(sleep, total) / 100u,
           prv_percentage(sleep, total) % 100u);
  prompt_send_response(buffer);
  snprintf(buffer, sizeof(buffer), "Total: %" PRIu64 " ticks", total);
  prompt_send_response(buffer);
}

void pbl_analytics_external_collect_cpu_stats(void) {
  const RtcTicks now = rtc_get_ticks();
  const RtcTicks total = now - s_last_stats_ticks;
  const RtcTicks sleep = s_wfi_ticks < total ? s_wfi_ticks : total;
  const RtcTicks running = total - sleep;

  PBL_ANALYTICS_SET_UNSIGNED(cpu_running_pct,
                             prv_percentage(running, total));
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep0_pct,
                             prv_percentage(sleep, total));
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep1_pct, 0);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep2_pct, 0);

  s_last_stats_ticks = now;
  s_wfi_ticks = 0;
}
