/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <pbl/services/regular_timer.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/soc/t5ai/tick.h>

#ifdef CONFIG_T5AI_POWER_PROFILE
#include <pbl/drivers/t5ai/sd.h>
#ifdef CONFIG_ACCEL_QMI8658_T5AI
#include <pbl/drivers/imu/qmi8658.h>
#endif
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/power.h>
#endif

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
#ifdef CONFIG_T5AI_POWER_PROFILE
  T5AIClockProfile clock;
  t5ai_power_clock_profile(&clock);
  PBL_LOG_DBG("T5CLOCK mode=%08lx c0=%08lx c1=%08lx ana9=%08lx cpu0=%lu ap=%lu",
              (unsigned long)clock.mode, (unsigned long)clock.cpu0, (unsigned long)clock.cpu1,
              (unsigned long)clock.ana9, (unsigned long)clock.cpu0_hz, (unsigned long)clock.ap_hz);
  T5AISDPowerStats sd;
  t5ai_sd_power_stats(&sd);
  uint32_t lost = 0u;
#ifdef CONFIG_T5AI_PM_WAKE_EVENTS
  lost = t5ai_cp_mailbox_wake_events_lost();
#endif
  PBL_LOG_DBG("T5POWER cpu=%lu sd_acq=%lu rel=%lu held=%lu err=%lu",
              (unsigned long)t5ai_power_clock_hz(), (unsigned long)sd.acquires,
              (unsigned long)sd.releases, (unsigned long)sd.fallbacks, (unsigned long)sd.errors);
  PBL_LOG_DBG("T5POWER_IO rd=%lu wr=%lu active=%u lost=%lu", (unsigned long)sd.read_bytes,
              (unsigned long)sd.written_bytes, sd.clock_active, (unsigned long)lost);
#ifdef CONFIG_ACCEL_QMI8658_T5AI
  T5AIQMIPowerStats imu;
  t5ai_qmi8658_power_stats(&imu);
  const unsigned flags = imu.identity_valid | (imu.initialized << 1) | (imu.active << 2) |
                         (imu.shake << 3) | (imu.recovery << 4) | (imu.irq_armed << 5);
  PBL_LOG_DBG("T5IMU id=%u rev=%u flags=%x odr=%u wm=%u us=%lu batch=%lu", imu.id, imu.revision,
              flags, imu.odr, imu.watermark, (unsigned long)imu.interval_us,
              (unsigned long)imu.batch);
  PBL_LOG_DBG("T5IMU_WORK irq=%lu timer=%lu passes=%lu acquired=%lu", (unsigned long)imu.irqs,
              (unsigned long)imu.watchdogs, (unsigned long)imu.passes, (unsigned long)imu.acquired);
#endif
#endif
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
