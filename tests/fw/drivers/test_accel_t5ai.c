/* SPDX-License-Identifier: Apache-2.0 */
#include "../../../drivers/imu/qmi8658/qmi8658_t5ai.h"

#include <errno.h>

#include <pbl/device.h>
#include <pbl/drivers/accel.h>
#include <pbl/services/new_timer/new_timer.h>

#include <clar.h>
#include <fake_mutex.h>

static unsigned s_bus_init, s_sensor_init, s_samples, s_events;
static uint32_t s_interval, s_count;
static bool s_shake, s_queue_full, s_peek_fail;
static NewTimerWorkCallback s_work;
static void *s_work_data;

static int prv_bus_init(const struct pbl_device *dev) {
  s_bus_init++;
  return 0;
}
PBL_DEVICE_STATE_DEFINE(t5ai_i2c0_device);
const struct pbl_device t5ai_i2c0_device =
    PBL_DEVICE_INIT(t5ai_i2c0_device, "i2c0", prv_bus_init, nullptr, nullptr);

void accel_init(void) {
  cl_assert_equal_i(s_bus_init, 1);
  s_sensor_init++;
}
bool qmi8658_is_initialized(void) {
  return true;
}
void accel_set_rotated(bool rotated) {
}
uint32_t accel_set_sampling_interval(uint32_t us) {
  return s_interval = us;
}
uint32_t accel_get_sampling_interval(void) {
  return s_interval;
}
void accel_set_num_samples(uint32_t count) {
  s_count = count;
}
uint32_t accel_get_max_num_samples(void) {
  return 128;
}
int accel_peek(AccelDriverSample *sample) {
  return s_peek_fail ? -1 : 0;
}
void accel_enable_shake_detection(bool on) {
  s_shake = on;
}
bool accel_get_shake_detection_enabled(void) {
  return s_shake;
}
void accel_set_shake_sensitivity_high(bool high) {
}
void accel_set_shake_sensitivity_percent(uint8_t percent) {
}

bool new_timer_add_work_callback(NewTimerWorkCallback cb, void *data) {
  if (s_queue_full) {
    return false;
  }
  s_work = cb;
  s_work_data = data;
  return true;
}

static void prv_samples(const struct pbl_accel *accel, const AccelRawBatch *batch, void *data) {
  cl_assert(pbl_mutex_is_owner(&accel->state->lock));
  s_samples += batch->num_samples;
}
static void prv_event(const struct pbl_accel *accel, const struct pbl_accel_event *event,
                      void *data) {
  cl_assert(pbl_mutex_is_owner(&accel->state->lock));
  cl_assert_equal_i(event->type, PBL_ACCEL_EVENT_SHAKE);
  cl_assert_equal_i(event->direction, -1);
  s_events++;
}
static const struct pbl_accel_listener s_listener = {.samples = prv_samples, .event = prv_event};

void test_accel_t5ai__initialize(void) {
  cl_assert_equal_i(pbl_device_init(&ACCEL->dev), 0);
  cl_assert_equal_i(s_bus_init, 1);
  cl_assert_equal_i(s_sensor_init, 1);
  s_queue_full = s_peek_fail = false;
  s_samples = s_events = 0;
  s_work = nullptr;
  pbl_accel_set_listener(ACCEL, &s_listener);
}

static void prv_deliver(void) {
  cl_assert(pbl_mutex_is_owner(&ACCEL->state->lock));
  const AccelDriverSample sample = {.x = 100};
  accel_cb_new_sample(&sample);
  const uint8_t raw[12] = {};
  const AccelRawBatch batch = {.data = raw, .num_samples = 2, .stride = 6, .scale_den = 1};
  accel_cb_new_samples(&batch);
  accel_cb_shake_detected(0, -1);
}

void test_accel_t5ai__queued_reports_hold_the_device_lock(void) {
  cl_assert_equal_i(pbl_accel_set_shake_detection(ACCEL, true), 0);
  cl_assert(accel_offload_work(prv_deliver));
  cl_assert_equal_i(s_samples, 0);
  s_work(s_work_data);
  cl_assert_equal_i(s_samples, 3);
  cl_assert_equal_i(s_events, 1);
  cl_assert(!pbl_mutex_is_owner(&ACCEL->state->lock));
  s_queue_full = true;
  cl_assert(!accel_offload_work(prv_deliver));
}

void test_accel_t5ai__class_operations_reach_the_hardware(void) {
  uint32_t actual;
  cl_assert_equal_i(pbl_accel_set_sampling_interval(ACCEL, 16000, &actual), 0);
  cl_assert_equal_i(actual, 16000);
  cl_assert_equal_i(pbl_accel_get_sampling_interval(ACCEL), 16000);
  cl_assert_equal_i(pbl_accel_set_num_samples(ACCEL, 4), 0);
  cl_assert_equal_i(s_count, 4);
  AccelDriverSample sample;
  cl_assert_equal_i(pbl_accel_peek(ACCEL, &sample), 0);
  s_peek_fail = true;
  cl_assert_equal_i(pbl_accel_peek(ACCEL, &sample), -EIO);
  cl_assert_equal_i(pbl_accel_set_double_tap_detection(ACCEL, true), -ENOTSUP);
}
