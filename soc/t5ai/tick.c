/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/idle.h>
#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/tick.h>
#include <stdint.h>

#define AON_TICKS_PER_KERNEL_TICK (T5AI_AON_RTC_HZ / PBL_TICK_HZ)
_Static_assert(T5AI_AON_RTC_HZ % PBL_TICK_HZ == 0u, "AON must divide into kernel ticks");

static uint32_t s_accounted;

void t5ai_tick_start(void) {
  s_accounted = T5AI_AON_RTC_COUNT_LOW;
}

void SysTick_Handler(void) {
  t5ai_power_sync_tick_clock();
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t elapsed = (now - s_accounted) / AON_TICKS_PER_KERNEL_TICK;
  if (elapsed == 0u) {
    return;
  }
  // Keep fractional AON ticks, including across the low-word wrap.
  s_accounted += elapsed * AON_TICKS_PER_KERNEL_TICK;
  // Pending SysTicks coalesce while masked. Advance time once, not in an ISR loop.
  if (elapsed > 1u) {
    pbl_idle_slept(elapsed - 1u);
  }
  pbl_kernel_tick_isr();
}
