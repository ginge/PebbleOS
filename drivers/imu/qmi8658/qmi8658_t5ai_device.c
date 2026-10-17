/* SPDX-License-Identifier: Apache-2.0 */

#include "qmi8658_t5ai.h"

#include <errno.h>

#include <pbl/services/new_timer/new_timer.h>

#include <system/status_codes.h>

static int prv_init(const struct pbl_accel *accel) {
  accel_init();
#ifdef CONFIG_T5AI_ACCEL_DISABLED
  return 0;
#else
  return qmi8658_is_initialized() ? 0 : -ENODEV;
#endif
}

static int prv_set_rotated(const struct pbl_accel *accel, bool rotated) {
  accel_set_rotated(rotated);
  return 0;
}

static int prv_set_interval(const struct pbl_accel *accel, uint32_t interval_us,
                            uint32_t *actual_us) {
  *actual_us = accel_set_sampling_interval(interval_us);
  return 0;
}

static uint32_t prv_get_interval(const struct pbl_accel *accel) {
  return accel_get_sampling_interval();
}

static int prv_set_num_samples(const struct pbl_accel *accel, uint32_t num_samples) {
  accel_set_num_samples(num_samples);
  return 0;
}

static uint32_t prv_get_max_num_samples(const struct pbl_accel *accel) {
  return accel_get_max_num_samples();
}

static int prv_peek(const struct pbl_accel *accel, AccelDriverSample *sample) {
  return accel_peek(sample) == S_SUCCESS ? 0 : -EIO;
}

static int prv_set_shake(const struct pbl_accel *accel, bool on) {
  accel_enable_shake_detection(on);
  return accel_get_shake_detection_enabled() == on ? 0 : -EIO;
}

static int prv_set_sensitivity_high(const struct pbl_accel *accel, bool high) {
  accel_set_shake_sensitivity_high(high);
  return 0;
}

static int prv_set_sensitivity_percent(const struct pbl_accel *accel, uint8_t percent) {
  accel_set_shake_sensitivity_percent(percent);
  return 0;
}

static const struct pbl_accel_ops s_ops = {
  .init = prv_init,
  .set_rotated = prv_set_rotated,
  .set_sampling_interval = prv_set_interval,
  .get_sampling_interval = prv_get_interval,
  .set_num_samples = prv_set_num_samples,
  .get_max_num_samples = prv_get_max_num_samples,
  .peek = prv_peek,
  .set_shake_detection = prv_set_shake,
  .set_shake_sensitivity_high = prv_set_sensitivity_high,
  .set_shake_sensitivity_percent = prv_set_sensitivity_percent,
};

extern const struct pbl_device t5ai_i2c0_device;

PBL_ACCEL_STATE_DEFINE(s_qmi8658);
static const struct pbl_accel s_qmi8658 =
    PBL_ACCEL_INIT(s_qmi8658, "qmi8658", &t5ai_i2c0_device, &s_ops);
PBL_DEVICE_REGISTER(s_qmi8658, &s_qmi8658.dev);
const struct pbl_accel *const ACCEL = &s_qmi8658;

void accel_cb_new_sample(const AccelDriverSample *sample) {
  pbl_accel_report_sample(ACCEL, sample);
}

void accel_cb_new_samples(const AccelRawBatch *batch) {
  pbl_accel_report_samples(ACCEL, batch);
}

void accel_cb_shake_detected(IMUCoordinateAxis axis, int32_t direction) {
  const struct pbl_accel_event event = {
    .type = PBL_ACCEL_EVENT_SHAKE,
    .axis = axis,
    .direction = direction,
  };
  pbl_accel_report_event(ACCEL, &event);
}

static void prv_offload(void *data) {
  void (*callback)(void) = data;
  pbl_accel_lock(ACCEL);
  callback();
  pbl_accel_unlock(ACCEL);
}

bool accel_offload_work(void (*callback)(void)) {
  return new_timer_add_work_callback(prv_offload, callback);
}
