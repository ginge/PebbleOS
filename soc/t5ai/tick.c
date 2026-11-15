/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/timer.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/t5ai/power.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/tick.h>

#include <cmsis_core.h>

#ifdef CONFIG_T5AI_TICK_PROFILE
T5AITickProfile t5ai_tick_profile;
#endif

static uint32_t s_deadline;
static uint32_t s_saved_ctrl;

static uint32_t prv_read(const struct pbl_clocksource *cs) {
  return T5AI_AON_RTC_COUNT_LOW;
}
static int prv_source_init(const struct pbl_device *dev);
static const struct pbl_clocksource_ops s_source_ops = {.read = prv_read};
PBL_DEVICE_STATE_DEFINE(s_aon);
static const struct pbl_clocksource s_aon =
    PBL_CLOCKSOURCE_INIT(s_aon, "t5ai-aon", prv_source_init, &s_source_ops, 32);
const struct pbl_clocksource *const pbl_system_clocksource = &s_aon;
static int prv_source_init(const struct pbl_device *dev) {
  pbl_clocksource_set_hz(&s_aon, T5AI_AON_RTC_HZ);
  return 0;
}
static void prv_shutdown(const struct pbl_clockevent *ce) {
  SysTick->CTRL = 0u;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
}
static void prv_set_next(const struct pbl_clockevent *ce, uint32_t delta) {
  prv_shutdown(ce);
  s_deadline =
      T5AI_AON_RTC_COUNT_LOW + (uint32_t)(((uint64_t)delta * T5AI_AON_RTC_HZ) / SystemCoreClock);
  SysTick->LOAD = delta - 1u;
  SysTick->VAL = 0u;
  SysTick->CTRL = SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk;
}
static int prv_event_init(const struct pbl_device *dev);
static const struct pbl_clockevent_ops s_event_ops = {
  .set_next = prv_set_next,
  .shutdown = prv_shutdown,
};
PBL_DEVICE_STATE_DEFINE(s_systick);
static const struct pbl_clockevent s_systick =
    PBL_CLOCKEVENT_INIT(s_systick, "t5ai-systick", prv_event_init, &s_event_ops, 64u, 1u << 24);
const struct pbl_clockevent *const pbl_system_clockevent = &s_systick;
static int prv_event_init(const struct pbl_device *dev) {
  prv_shutdown(&s_systick);
  t5ai_power_update_clock();
  NVIC_SetPriority(SysTick_IRQn, PBL_IRQ_PRIO_KERNEL);
  pbl_clockevent_set_hz(&s_systick, SystemCoreClock);
  return 0;
}
void SysTick_Handler(void) {
#ifdef CONFIG_T5AI_TICK_PROFILE
  ++t5ai_tick_profile.irqs;
  ++t5ai_tick_profile.ticks;
#endif
  SysTick->CTRL = 0u;
  t5ai_power_sync_tick_clock();
  pbl_clockevent_handler(&s_systick);
}
void t5ai_tick_start(void) {
}

#ifdef CONFIG_T5AI_RETAINED_SLEEP
uint32_t t5ai_tick_idle_deadline(uint32_t max_ticks) {
  return s_deadline;
}
bool t5ai_tick_prepare_retained(void) {
  uint32_t ctrl = SysTick->CTRL;
  if (!(ctrl & SysTick_CTRL_ENABLE_Msk) || s_saved_ctrl != 0 ||
      (SCB->ICSR & (SCB_ICSR_PENDSTSET_Msk | SCB_ICSR_VECTPENDING_Msk)))
    return false;
  SysTick->CTRL = 0;
  __DSB();
  if (SCB->ICSR & (SCB_ICSR_PENDSTSET_Msk | SCB_ICSR_VECTPENDING_Msk)) {
    SysTick->CTRL = ctrl;
    return false;
  }
  s_saved_ctrl = ctrl;
  return true;
}
#endif
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_TICKLESS_WFI)
void t5ai_tick_resume_idle(void) {
  if (s_saved_ctrl == 0)
    return;
  s_saved_ctrl = 0;
  t5ai_power_sync_tick_clock();
  const int32_t remaining = (int32_t)(s_deadline - T5AI_AON_RTC_COUNT_LOW);
  uint64_t cycles =
      remaining > 0 ? ((uint64_t)(uint32_t)remaining * SystemCoreClock) / T5AI_AON_RTC_HZ : 64;
  if (cycles < 64)
    cycles = 64;
  if (cycles > (1u << 24))
    cycles = (1u << 24);
  prv_set_next(&s_systick, (uint32_t)cycles);
}
bool t5ai_tick_prepare_idle(uint32_t max_ticks) {
  return false;
}
#endif
