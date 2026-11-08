/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/native_alarm.h>

#define ALARM_TEST_DELAY_TICKS 3200u
#define ALARM_TEST_INTERVAL_MS 1000u
#define ALARM_TEST_WAIT_STEPS  2u
#define ALARM_TEST_FINISH_STEP 5u
PBL_LOG_MODULE_DEFINE(t5ai_alarm_test, LOG_LEVEL_DEBUG);
static unsigned s_step;
static uint32_t s_test_generation;
static bool s_test_stopped;
static TimerID s_test_timer;

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
    new_timer_delete(s_test_timer);
    s_test_timer = TIMER_INVALID_ID;
  }
}

static void prv_schedule(void *context) {
  (void)context;
  if (!s_test_stopped)
    (void)system_task_add_callback_droppable(prv_step, NULL);
}

void t5ai_native_alarm_test_start(void) {
  if (s_test_stopped || s_test_timer != TIMER_INVALID_ID)
    return;
  s_test_timer = new_timer_create();
  (void)new_timer_start(s_test_timer, ALARM_TEST_INTERVAL_MS, prv_schedule, NULL,
                        TIMER_START_FLAG_REPEATING);
}
