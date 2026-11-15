/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "../../../drivers/imu/qmi8658/qmi8658_t5ai.c"

#include <errno.h>
#include <string.h>

#include <pbl/device.h>
#include <pbl/drivers/imu/qmi8658.h>

#include <clar.h>
#include <fake_mutex.h>
#include <fake_rtc.h>

static unsigned s_samples, s_events, s_bus_init, s_irq_init;
static bool s_bus_fail, s_queue_full, s_fail_shake_rate;
static NewTimerWorkCallback s_work;
static void *s_work_data;
static int test_bus_init(const struct pbl_device *dev) {
  ++s_bus_init;
  return 0;
}
static int test_irq_init(const struct pbl_device *dev) {
  ++s_irq_init;
  return 0;
}
PBL_DEVICE_STATE_DEFINE(s_bus);
static struct pbl_i2c_bus s_bus = {
  .dev = PBL_DEVICE_INIT(s_bus, "i2c", test_bus_init, nullptr, nullptr)
};
PBL_DEVICE_STATE_DEFINE(s_intc);
static const struct pbl_intc s_intc = {
  .dev = PBL_DEVICE_INIT(s_intc, "irq", test_irq_init, nullptr, nullptr)
};
static const struct pbl_gpio_port s_port = {};
PBL_QMI8658_DEFINE(s_sensor, &s_bus, 0x6b, &s_port, &s_intc, 9);

void pbl_i2c_use(const struct pbl_i2c_dev *dev) {
  cl_assert(pbl_device_is_ready(&dev->bus->dev));
  cl_assert(pbl_mutex_is_owner(&s_sensor.state->io_lock));
}
void pbl_i2c_release(const struct pbl_i2c_dev *dev) {
}
int pbl_i2c_write_block(const struct pbl_i2c_dev *dev, uint32_t len, const uint8_t *data) {
  if (s_fail_shake_rate && len == 2 && data[0] == QMI8658_CTRL2 &&
      data[1] == (QMI8658_ACCEL_RANGE_4G | QMI8658_ODR_31_25HZ))
    return -EIO;
  return s_bus_fail ? -EIO : 0;
}
int pbl_i2c_read_register_block(const struct pbl_i2c_dev *dev, uint8_t reg, uint32_t len,
                                uint8_t *data) {
  memset(data, 0, len);
  if (reg == QMI8658_WHO_AM_I)
    data[0] = QMI8658_ID;
  if (reg == QMI8658_STATUSINT)
    data[0] = QMI8658_CMD_DONE;
  return s_bus_fail ? -EIO : 0;
}
int pbl_gpio_configure(const struct pbl_gpio *gpio, uint32_t flags) {
  return 0;
}
int pbl_gpio_get_raw(const struct pbl_gpio *gpio) {
  return 0;
}
int pbl_intc_request(const struct pbl_irq_spec *irq, pbl_intc_handler_t cb, void *data) {
  cl_assert(pbl_device_is_ready(&irq->intc->dev));
  return 0;
}
int pbl_intc_free(const struct pbl_irq_spec *irq) {
  return 0;
}
int pbl_intc_enable(const struct pbl_irq_spec *irq) {
  return 0;
}
int pbl_intc_disable(const struct pbl_irq_spec *irq) {
  return 0;
}
TimerID new_timer_create(void) {
  return 1;
}
void new_timer_delete(TimerID timer) {
}
bool new_timer_start(TimerID timer, uint32_t ms, NewTimerCallback cb, void *data, uint32_t flags) {
  return true;
}
bool new_timer_stop(TimerID timer) {
  return true;
}
bool new_timer_add_work_callback(NewTimerWorkCallback cb, void *data) {
  if (s_queue_full)
    return false;
  s_work = cb;
  s_work_data = data;
  return true;
}
static void test_samples(const struct pbl_accel *accel, const AccelRawBatch *batch, void *data) {
  cl_assert(pbl_mutex_is_owner(&accel->state->lock));
  s_samples += batch->num_samples;
}
static void test_event(const struct pbl_accel *accel, const struct pbl_accel_event *event,
                       void *data) {
  cl_assert(pbl_mutex_is_owner(&accel->state->lock));
  s_events++;
}
static const struct pbl_accel_listener s_listener = {.samples = test_samples, .event = test_event};

void test_accel_t5ai__initialize(void) {
  s_bus_fail = s_queue_full = s_fail_shake_rate = false;
  s_samples = s_events = 0;
  s_work = nullptr;
  cl_assert_equal_i(pbl_device_init(&s_sensor.accel.dev), 0);
  cl_assert_equal_i(s_bus_init, 1);
  cl_assert_equal_i(s_irq_init, 1);
  pbl_accel_set_listener(&s_sensor.accel, &s_listener);
}
void test_accel_t5ai__queued_delivery_holds_the_device_lock(void) {
  s_sensor.state->active = true;
  s_sensor.state->num_samples = 1;
  s_sensor.state->pending_count = 1;
  s_sensor.state->pending_first_us = 200000;
  s_sensor.state->startup_us = 0;
  s_sensor.state->sample_generation = s_sensor.state->generation;
  cl_assert(prv_queue_work(prv_deliver_sample));
  cl_assert_equal_i(s_samples, 0);
  s_work(s_work_data);
  cl_assert_equal_i(s_samples, 1);
  cl_assert(!pbl_mutex_is_owner(&s_sensor.accel.state->lock));
  s_queue_full = true;
  cl_assert(!prv_queue_work(prv_deliver_sample));
}
void test_accel_t5ai__invalid_batch_and_failed_rate_change_report_errors(void) {
  cl_assert_equal_i(pbl_accel_set_num_samples(&s_sensor.accel, 7), -EINVAL);
  const uint32_t previous = s_sensor.state->interval_us;
  s_bus_fail = true;
  uint32_t actual;
  cl_assert_equal_i(pbl_accel_set_sampling_interval(&s_sensor.accel, 16000, &actual), -EIO);
  cl_assert_equal_i(s_sensor.state->interval_us, previous);
  cl_assert_equal_i(pbl_accel_set_double_tap_detection(&s_sensor.accel, true), -ENOTSUP);
}
void test_accel_t5ai__failed_initialization_does_not_mark_the_sensor_ready(void) {
  struct pbl_device_state *state = s_sensor.accel.dev.state;
  state->status = PBL_DEVICE_UNINIT;
  s_bus_fail = true;
  cl_assert_equal_i(pbl_device_init(&s_sensor.accel.dev), -EIO);
  cl_assert(!pbl_device_is_ready(&s_sensor.accel.dev));
  s_bus_fail = false;
  state->status = PBL_DEVICE_UNINIT;
  cl_assert_equal_i(pbl_device_init(&s_sensor.accel.dev), 0);
}

void test_accel_t5ai__failed_shake_rate_change_rolls_back_detection(void) {
  cl_assert_equal_i(pbl_accel_set_num_samples(&s_sensor.accel, 0), 0);
  cl_assert_equal_i(pbl_accel_set_shake_detection(&s_sensor.accel, false), 0);
  cl_assert_equal_i(pbl_accel_set_sampling_interval(&s_sensor.accel, 333000, nullptr), 0);
  s_fail_shake_rate = true;
  cl_assert_equal_i(pbl_accel_set_shake_detection(&s_sensor.accel, true), -EIO);
  cl_assert(!pbl_accel_get_shake_detection(&s_sensor.accel));
  cl_assert(!s_sensor.state->shake_enabled);
  cl_assert(!s_sensor.state->active);
  cl_assert_equal_i(s_sensor.state->interval_us, 333000);
}
