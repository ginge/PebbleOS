/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Optional on-watch FIFO experiment, excluded from normal builds.
#ifdef QMI8658_FIFO_SELF_TEST
static struct {
  bool precheck;
  TimerID timer;
  unsigned phase;
  uint32_t interval;
  uint32_t count;
  bool shake;
  bool rotated;
  bool pending;
} s_test;
static void prv_self_test_schedule(void *context);
static void prv_self_test_step(void) {
  PBL_LOG_DBG("QMITEST phase=%u", s_test.phase);
  PBL_LOG_DBG("QMITEST previous valid=%u x=%d y=%d z=%d", s_dev->state->last_sample_valid,
              s_dev->state->last_sample.x, s_dev->state->last_sample.y,
              s_dev->state->last_sample.z);
  switch (s_test.phase++) {
    case 0:
      s_test.interval = s_dev->state->requested_interval_us;
      s_test.count = s_dev->state->num_samples;
      s_test.shake = s_dev->state->shake_enabled;
      s_test.rotated = s_dev->state->rotated;
      pbl_accel_set_shake_detection(&s_dev->accel, false);
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 8000u, nullptr);
      pbl_accel_set_num_samples(&s_dev->accel, 2u);
      s_test.precheck = true;
      break;
    case 1:
      s_test.precheck = false;
      break;
    case 2:
      pbl_accel_set_num_samples(&s_dev->accel, 4u);
      break;
    case 3:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 16000u, nullptr);
      break;
    case 4:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 32000u, nullptr);
      break;
    case 5:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 91000u, nullptr);
      pbl_accel_set_num_samples(&s_dev->accel, 2u);
      break;
    case 6:
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, 333000u, nullptr);
      break;
    default:
      pbl_accel_set_rotated(&s_dev->accel, s_test.rotated);
      pbl_accel_set_shake_detection(&s_dev->accel, s_test.shake);
      (void)pbl_accel_set_sampling_interval(&s_dev->accel, s_test.interval, nullptr);
      pbl_accel_set_num_samples(&s_dev->accel, s_test.count);
      (void)new_timer_stop(s_test.timer);
      PBL_LOG_DBG("QMITEST restored");
      s_test.pending = false;
      return;
  }
  PBL_LOG_DBG("QMITEST state interval=%lu batch=%lu shake=%u rotated=%u active=%u",
              (unsigned long)s_dev->state->interval_us, (unsigned long)s_dev->state->num_samples,
              s_dev->state->shake_enabled, s_dev->state->rotated, s_dev->state->active);
  (void)new_timer_start(s_test.timer, 15000u, prv_self_test_schedule, NULL,
                        TIMER_START_FLAG_REPEATING);
  s_test.pending = false;
}
static void prv_self_test_schedule(void *context) {
  (void)context;
  if (!s_test.pending) {
    s_test.pending = true;
    if (!prv_queue_work(prv_self_test_step)) {
      s_test.pending = false;
    }
  }
}
static void prv_self_test_start(void) {
  s_test.timer = new_timer_create();
  if (s_test.timer != TIMER_INVALID_ID) {
    (void)new_timer_start(s_test.timer, 45000u, prv_self_test_schedule, NULL, 0);
  }
}
#endif
