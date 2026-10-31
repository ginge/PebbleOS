/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <pbl/services/regular_timer.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/tick.h>

PBL_LOG_MODULE_DEFINE(t5ai_tick_profile, LOG_LEVEL_DEBUG);
static uint32_t s_started;
static bool s_registered;

static void prv_dump(void *context) {
  (void)context;
  pbl_irq_lock();
  const uint32_t now = T5AI_AON_RTC_COUNT_LOW;
  const uint32_t elapsed = now - s_started;
  const T5AITickProfile p = t5ai_tick_profile;
  t5ai_tick_profile = (T5AITickProfile){0};
  s_started = now;
  pbl_irq_unlock();
  PBL_LOG_DBG("T5TICK aon=%lu irq=%lu ticks=%lu max=%lu extended=%lu early=%lu aborted=%lu",
              (unsigned long)elapsed, (unsigned long)p.irqs, (unsigned long)p.ticks,
              (unsigned long)p.maximum, (unsigned long)p.extended, (unsigned long)p.early,
              (unsigned long)p.aborted);
}

static void prv_schedule(void *context) {
  (void)context;
  (void)system_task_add_callback_droppable(prv_dump, nullptr);
}

void t5ai_tick_profile_start(void) {
  if (s_registered) {
    return;
  }
  pbl_irq_lock();
  t5ai_tick_profile = (T5AITickProfile){0};
  s_started = T5AI_AON_RTC_COUNT_LOW;
  pbl_irq_unlock();
  static RegularTimerInfo timer = {.cb = prv_schedule};
  regular_timer_add_multisecond_callback(&timer, 60u);
  s_registered = true;
}
