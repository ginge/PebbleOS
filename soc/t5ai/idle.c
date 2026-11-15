/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/analytics/analytics.h>
#include <pbl/kernel/idle.h>
#include <pbl/kernel/time.h>
#include <pbl/soc/t5ai/sleep.h>
#include <pbl/soc/t5ai/tick.h>
#include <pbl/util/size.h>

#include <kernel.h>

static void prv_idle(void) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP
  const uint32_t deadline = t5ai_tick_idle_deadline(UINT32_MAX);
  t5ai_sleep_service(deadline);
  if (!sched_idle_confirm())
    return;
  if (t5ai_sleep_enter(deadline))
    return;
#endif
  pbl_cpu_idle();
}

const struct pbl_idle_state pbl_soc_idle_states[] = {
  {.name = "wfi", .min_ticks = 1, .exit_ticks = 0, .enter = prv_idle},
};
const size_t pbl_soc_num_idle_states = ARRAY_LENGTH(pbl_soc_idle_states);
int pbl_soc_idle_state_limit(void) {
  return 0;
}
void pbl_analytics_external_collect_cpu_stats(void) {
  static uint64_t s_last_us, s_last_sleep_us;
  const uint64_t now = pbl_uptime_ms() * 1000u;
  const uint64_t sleep_now = pbl_idle_residency_us(0);
  const uint64_t total = now - s_last_us;
  const uint64_t slept = sleep_now - s_last_sleep_us;
  const uint32_t sleep_pct = total ? (uint32_t)(slept * 10000u / total) : 0;
  PBL_ANALYTICS_SET_UNSIGNED(cpu_running_pct, 10000u - sleep_pct);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep0_pct, sleep_pct);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep1_pct, 0);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep2_pct, 0);
  s_last_us = now;
  s_last_sleep_us = sleep_now;
}
