/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/native_alarm.h>

#define ALARM_TEST_DELAY_TICKS 3200u
#define ALARM_TEST_INTERVAL_MS 1000u
#define ALARM_TEST_WAIT_STEPS  2u
#define ALARM_TEST_FINISH_STEP 5u
PBL_LOG_MODULE_DEFINE(t5ai_alarm_test, LOG_LEVEL_DEBUG);
static unsigned s_step;
static uint32_t s_test_generation;
static bool s_test_stopped;
static struct pbl_delayed_work s_test_timer;
static bool s_started;

static void prv_step(void *context) {
  (void)context;
  if (s_test_stopped)
    return;
  status_t status = S_SUCCESS;
  uint32_t wake_tick = 0u;
  const uint32_t checked_generation = s_test_generation;
  const bool fired = t5ai_native_alarm_fired(s_test_generation, &wake_tick);
  switch (s_step) {
    case 0u:
      status = t5ai_native_alarm_init();
      if (status == S_SUCCESS)
        status = t5ai_native_alarm_arm(ALARM_TEST_DELAY_TICKS, &s_test_generation);
      break;
    case ALARM_TEST_WAIT_STEPS:
      if (!fired) {
        status = E_AGAIN;
        break;
      }
      status = t5ai_native_alarm_cancel(s_test_generation);
      if (status == S_SUCCESS)
        status = t5ai_native_alarm_arm(T5AI_PM_NATIVE_ALARM_MAX_TICKS, &s_test_generation);
      if (status == S_SUCCESS)
        status = t5ai_native_alarm_cancel(s_test_generation);
      break;
    case ALARM_TEST_FINISH_STEP:
      if (fired || !t5ai_native_alarm_healthy())
        status = E_ERROR;
      s_test_stopped = true;
      break;
    default:
      break;
  }
  PBL_LOG_DBG("T5ALARM step=%u status=%ld gen=%lu checked=%lu fired=%u tick=%lu", s_step,
              (long)status, (unsigned long)s_test_generation, (unsigned long)checked_generation,
              fired, (unsigned long)wake_tick);
  if (status != S_SUCCESS)
    s_test_stopped = true;
  ++s_step;
  if (s_test_stopped) {
    pbl_delayed_work_cancel(&s_test_timer);
    s_started = false;
  }
}

static void prv_schedule(struct pbl_work *context) {
  (void)context;
  if (!s_test_stopped) {
    (void)pbl_sys_work_try_call(prv_step, NULL);
    pbl_delayed_work_schedule_next(&s_test_timer, pbl_ms_to_ticks(ALARM_TEST_INTERVAL_MS));
  }
}

void t5ai_native_alarm_test_start(void) {
  if (s_test_stopped || s_started)
    return;
  s_started = true;
  pbl_delayed_work_init(&s_test_timer, prv_schedule);
  (void)pbl_delayed_work_schedule(&pbl_sys_workq_high, &s_test_timer,
                                  PBL_MSEC(ALARM_TEST_INTERVAL_MS));
}
