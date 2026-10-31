/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/kernel/idle.h>
#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/tick.h>

#include <cmsis_core.h>

#define AON_TICKS_PER_KERNEL_TICK (T5AI_AON_RTC_HZ / PBL_TICK_HZ)
_Static_assert(T5AI_AON_RTC_HZ % PBL_TICK_HZ == 0u, "AON must divide into kernel ticks");

static uint32_t s_accounted;

#ifdef CONFIG_T5AI_TICK_PROFILE
T5AITickProfile t5ai_tick_profile;
#define TICK_ADD(field, value) (t5ai_tick_profile.field += (value))
#else
#define TICK_ADD(field, value) ((void)0)
#endif

#ifdef CONFIG_T5AI_TICKLESS_WFI
#define T5AI_SYSTICK_MAX_CYCLES (1u << 24)
#define T5AI_SYSTICK_RUNNING \
  (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk)
static uint32_t s_idle_ctrl;

void t5ai_tick_resume_idle(void) {
  if (s_idle_ctrl == 0u) {
    return;
  }
  TICK_ADD(early, (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) == 0u);
  SysTick->CTRL = 0u;
  __DSB();
  t5ai_power_sync_tick_clock();
  SysTick->LOAD = SystemCoreClock / PBL_TICK_HZ - 1u;
  SysTick->VAL = 0u;
  SysTick->CTRL = s_idle_ctrl;
  s_idle_ctrl = 0u;
  // Let the existing ISR expire timers in their normal context, exactly once.
  if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - s_accounted) >= AON_TICKS_PER_KERNEL_TICK) {
    SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;
  }
}

bool t5ai_tick_prepare_idle(uint32_t max_ticks) {
  const uint32_t minimum_hz = t5ai_power_wfi_clock_min_hz();
  const uint32_t ctrl = SysTick->CTRL & T5AI_SYSTICK_RUNNING;
  if (minimum_hz == 0u || max_ticks < 3u || s_idle_ctrl != 0u || ctrl != T5AI_SYSTICK_RUNNING ||
      (SCB->SCR & SCB_SCR_SLEEPDEEP_Msk) != 0u ||
      (SCB->ICSR & (SCB_ICSR_PENDSTSET_Msk | SCB_ICSR_VECTPENDING_Msk)) != 0u) {
    return false;
  }
  const uint32_t fractional = T5AI_AON_RTC_COUNT_LOW - s_accounted;
  if (fractional >= AON_TICKS_PER_KERNEL_TICK) {
    // Keep the running periodic deadline; a synthetic tick would duplicate it.
    return false;
  }
  // Leave one kernel tick for setup/restore and the final periodic deadline.
  uint64_t aon_ticks = ((uint64_t)max_ticks - 1u) * AON_TICKS_PER_KERNEL_TICK - fractional;
  const uint32_t limit = (uint64_t)T5AI_SYSTICK_MAX_CYCLES * T5AI_AON_RTC_HZ / minimum_hz;
  if (aon_ticks > limit) {
    aon_ticks = limit;
  }
  const uint32_t cycles = aon_ticks * minimum_hz / T5AI_AON_RTC_HZ;
  // A conservative clock bound must not create more frequent tick interrupts.
  if (cycles <= SysTick->LOAD + 1u) {
    return false;
  }
  s_idle_ctrl = ctrl;
  SysTick->CTRL = 0u;
  __DSB();
  // Preserve an underflow that raced with stopping the original periodic tick.
  if ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0u) {
    TICK_ADD(aborted, 1u);
    t5ai_tick_resume_idle();
    return false;
  }
  SysTick->LOAD = cycles - 1u;
  SysTick->VAL = 0u;
  SysTick->CTRL = ctrl;
  if ((uint32_t)(T5AI_AON_RTC_COUNT_LOW - s_accounted) >= AON_TICKS_PER_KERNEL_TICK) {
    TICK_ADD(aborted, 1u);
    t5ai_tick_resume_idle();
    return false;
  }
  TICK_ADD(extended, 1u);
  return true;
}
#endif

void t5ai_tick_start(void) {
  s_accounted = T5AI_AON_RTC_COUNT_LOW;
}

#ifdef CONFIG_T5AI_APP_TRACE
#include <pbl/soc/t5ai/app_trace.h>
void __attribute__((naked)) SysTick_Handler(void) {
  __asm volatile(
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mov r1, lr\n"
      "push {r4, lr}\n"
      "bl t5ai_app_trace_tick\n"
      "pop {r4, lr}\n"
      "b t5ai_tick_body\n");
}
void __attribute__((used, noinline)) t5ai_tick_body(void) {
#else
void SysTick_Handler(void) {
#endif
  TICK_ADD(irqs, 1u);
  t5ai_power_sync_tick_clock();
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t elapsed = (now - s_accounted) / AON_TICKS_PER_KERNEL_TICK;
  TICK_ADD(ticks, elapsed);
#ifdef CONFIG_T5AI_TICK_PROFILE
  if (elapsed > t5ai_tick_profile.maximum) {
    t5ai_tick_profile.maximum = elapsed;
  }
#endif
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
