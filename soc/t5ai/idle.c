/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/idle.h>
#include <pbl/soc/t5ai/interrupt_profile.h>
#include <pbl/soc/t5ai/tick.h>

#include <kernel/util/idle.h>
#ifdef CONFIG_T5AI_RETAINED_SLEEP
#include <pbl/soc/t5ai/sleep.h>
#endif

#include "pbl/drivers/rtc.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/soc/t5ai/power.h"

#include <inttypes.h>
#include <stdio.h>

#include <cmsis_core.h>

static RtcTicks s_wfi_ticks;
static RtcTicks s_last_stats_ticks;

static uint32_t prv_percentage(RtcTicks value, RtcTicks total) {
  if (total == 0) {
    return 0;
  }
  return (uint32_t)((value * 10000u) / total);
}

void pbl_soc_idle(pbl_tick_t expected_idle_ticks) {
  (void)expected_idle_ticks;

  if (!idle_is_allowed()) {
    return;
  }

#ifdef CONFIG_T5AI_RETAINED_SLEEP
  const uint32_t deadline = t5ai_tick_idle_deadline(expected_idle_ticks);
  t5ai_sleep_service(deadline);
#endif
  const RtcTicks start = rtc_get_ticks();
  const uint32_t profile_start = t5ai_interrupt_profile_irq_enter();
  __disable_irq();
  if (pbl_idle_confirm()) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP
    if (t5ai_sleep_enter(deadline))
      goto woke;
#endif
#ifdef CONFIG_T5AI_TICKLESS_WFI
    const bool extended = t5ai_tick_prepare_idle(expected_idle_ticks);
#endif
    __DSB();
    __WFI();
    __ISB();
#ifdef CONFIG_T5AI_TICKLESS_WFI
    if (extended) {
      t5ai_tick_resume_idle();
    }
#endif
  }
#ifdef CONFIG_T5AI_RETAINED_SLEEP
woke:
#endif
  t5ai_interrupt_profile_irq_exit(3, profile_start);
  __enable_irq();
#ifdef CONFIG_T5AI_RETAINED_SLEEP
  t5ai_sleep_service(deadline);
#endif
  s_wfi_ticks += rtc_get_ticks() - start;
}

bool pbl_soc_tick_enable(void) {
  t5ai_power_update_clock();
  t5ai_interrupt_profile_start();
  t5ai_tick_start();
  return false;
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
