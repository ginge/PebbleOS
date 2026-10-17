/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "pbl/services/new_timer/new_timer.h"
#include "system/status_codes.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pbl/drivers/i2c.h>
#include <pbl/drivers/imu/qmi8658.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/util/misc.h>

#ifdef CONFIG_T5AI_QMI8658_PROFILE
PBL_LOG_MODULE_DEFINE(driver_accel_qmi8658, LOG_LEVEL_DEBUG);
#else
PBL_LOG_MODULE_DEFINE(driver_accel_qmi8658, CONFIG_DRIVER_IMU_LOG_LEVEL);
#endif

#define QMI8658_DEFAULT_INTERVAL_US 8000u
#define QMI8658_ACCEL_SAMPLE_BYTES  6u
#define QMI8658_ACCEL_X_OFFSET      0u
#define QMI8658_ACCEL_Y_OFFSET      2u
#define QMI8658_ACCEL_Z_OFFSET      4u
#define QMI8658_ODR_125HZ           0x06u
#define QMI8658_ODR_62_5HZ          0x07u
#define QMI8658_ODR_31_25HZ         0x08u
#define QMI8658_ODR_LOW_POWER_21HZ  0x0du
#define QMI8658_ODR_LOW_POWER_11HZ  0x0eu
#define QMI8658_ODR_LOW_POWER_3HZ   0x0fu

#define QMI8658_WHO_AM_I 0x00u
#define QMI8658_CTRL1    0x02u
#define QMI8658_CTRL2    0x03u
#define QMI8658_CTRL7    0x08u
#define QMI8658_STATUS0  0x2eu
#define QMI8658_AX_L     0x35u
#define QMI8658_RESET    0x60u

#define QMI8658_ID              0x05u
#define QMI8658_RESET_COMMAND   0xb0u
#define QMI8658_RESET_SETTLE_MS 10u
// CTRL1 startup configuration matches the pinned Waveshare sensor driver.
#define QMI8658_CTRL1_INIT        0x60u
#define QMI8658_ACCEL_RANGE_4G    0x10u
#define QMI8658_ACCEL_ENABLE      0x01u
#define QMI8658_ACCEL_DATA_READY  0x01u
#define QMI8658_COUNTS_PER_G      8192
#define QMI8658_STARTUP_SETTLE_MS 100u
#define QMI8658_SHAKE_INTERVAL_US 32000u
#define QMI8658_SHAKE_HOLDOFF_US  500000u
#define QMI8658_SHAKE_MIN_MG      175u
#define QMI8658_SHAKE_MAX_MG      1200u

typedef struct {
  uint32_t interval_us;
  uint8_t odr;
} QMI8658Rate;

// Poll intervals approximate the sensor ODRs; preserve the existing rounded periods.
static const QMI8658Rate s_rates[] = {
  {QMI8658_DEFAULT_INTERVAL_US, QMI8658_ODR_125HZ},
  {16000u, QMI8658_ODR_62_5HZ},
  {32000u, QMI8658_ODR_31_25HZ},
  {48000u, QMI8658_ODR_LOW_POWER_21HZ},
  {91000u, QMI8658_ODR_LOW_POWER_11HZ},
  {333000u, QMI8658_ODR_LOW_POWER_3HZ},
};

// One acquisition/delivery may be outstanding. Timer ticks while it is pending
// are dropped rather than building a backlog on the sensor worker or timer service.

static const struct pbl_qmi8658 *s_dev;
static bool prv_queue_work(void (*work)(void));
#include "qmi8658_profile.h"

static void prv_invalidate_poll(void) {
  pbl_irq_lock();
  ++s_dev->state->generation;
  pbl_irq_unlock();
}

static void prv_finish_poll(void) {
  pbl_irq_lock();
  s_dev->state->poll_pending = false;
  pbl_irq_unlock();
}

static bool prv_read(uint8_t reg, uint8_t *data, size_t length) {
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  pbl_i2c_use((&s_dev->i2c));
  const uint32_t started = qmi_profile_io_start();
  bool result = (pbl_i2c_write_block((&s_dev->i2c), 1, &reg) == 0);
  qmi_profile_io_end(started, 1u, result);
  if (result) {
    const uint32_t read_started = qmi_profile_io_start();
    result = (pbl_i2c_read_block((&s_dev->i2c), length, data) == 0);
    qmi_profile_io_end(read_started, length, result);
  }
  pbl_i2c_release((&s_dev->i2c));
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return result;
}

static bool prv_write(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  (void)pbl_mutex_lock(&s_dev->state->io_lock, PBL_FOREVER);
  pbl_i2c_use((&s_dev->i2c));
  const uint32_t started = qmi_profile_io_start();
  const bool result = (pbl_i2c_write_block((&s_dev->i2c), sizeof(data), data) == 0);
  qmi_profile_io_end(started, sizeof(data), result);
  pbl_i2c_release((&s_dev->i2c));
  pbl_mutex_unlock(&s_dev->state->io_lock);
  return result;
}

static uint64_t prv_time_us(void) {
  time_t seconds;
  uint16_t milliseconds;
  rtc_get_time_ms(&seconds, &milliseconds);
  return ((uint64_t)seconds * 1000u + milliseconds) * 1000u;
}

static int16_t prv_to_mg(const uint8_t *raw) {
  const int16_t value = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);
  return (int16_t)(((int32_t)value * 1000) / QMI8658_COUNTS_PER_G);
}

typedef enum {
  QMI8658_SAMPLE_ERROR,
  QMI8658_SAMPLE_NOT_READY,
  QMI8658_SAMPLE_READY,
} QMI8658SampleResult;

static QMI8658SampleResult prv_read_sample(AccelDriverSample *sample) {
  uint8_t status;
  uint8_t raw[QMI8658_ACCEL_SAMPLE_BYTES];
  if (!prv_read(QMI8658_STATUS0, &status, 1)) {
    PBL_LOG_ERR("QMI8658 status read failed");
    return QMI8658_SAMPLE_ERROR;
  }
  // Polling and sensor conversions are asynchronous; a clear bit is normal.
  if (!(status & QMI8658_ACCEL_DATA_READY)) {
    return QMI8658_SAMPLE_NOT_READY;
  }
  if (!prv_read(QMI8658_AX_L, raw, sizeof(raw))) {
    PBL_LOG_ERR("QMI8658 sample data read failed");
    return QMI8658_SAMPLE_ERROR;
  }
  const int16_t sensor_x = prv_to_mg(&raw[QMI8658_ACCEL_X_OFFSET]);
  const int16_t sensor_y = prv_to_mg(&raw[QMI8658_ACCEL_Y_OFFSET]);
  sample->x = sensor_y;
  sample->y = -sensor_x;
  sample->z = prv_to_mg(&raw[QMI8658_ACCEL_Z_OFFSET]);
  if (s_dev->state->rotated) {
    sample->x = -sample->x;
    sample->y = -sample->y;
  }
  sample->timestamp_us = prv_time_us();
  return QMI8658_SAMPLE_READY;
}

// Remove gravity with a low-pass baseline before comparing motion on each axis.
static void prv_detect_shake(const AccelDriverSample *sample) {
  if (!s_dev->state->shake_enabled) {
    return;
  }
  const int32_t values[] = {sample->x, sample->y, sample->z};
  const uint64_t now = sample->timestamp_us;
  if (!s_dev->state->shake_baseline_valid || now < s_dev->state->shake_previous_us ||
      now - s_dev->state->shake_previous_us > QMI8658_SHAKE_HOLDOFF_US) {
    for (size_t i = 0; i < 3; ++i) {
      s_dev->state->shake_baseline[i] = values[i];
    }
    s_dev->state->shake_baseline_valid = true;
    s_dev->state->shake_holdoff_us = now;
    s_dev->state->shake_previous_us = now;
    return;
  }
  s_dev->state->shake_previous_us = now;
  const uint32_t threshold =
      s_dev->state->shake_sensitivity_high
          ? QMI8658_SHAKE_MIN_MG
          : QMI8658_SHAKE_MAX_MG - s_dev->state->shake_sensitivity_percent *
                                       (QMI8658_SHAKE_MAX_MG - QMI8658_SHAKE_MIN_MG) / 100u;
  int32_t peak = 0;
  size_t axis = 0;
  for (size_t i = 0; i < 3; ++i) {
    const int32_t delta = values[i] - s_dev->state->shake_baseline[i];
    if ((delta < 0 ? -delta : delta) > (peak < 0 ? -peak : peak)) {
      peak = delta;
      axis = i;
    }
    s_dev->state->shake_baseline[i] += delta / 8;
  }
  if (now >= s_dev->state->shake_holdoff_us && (uint32_t)(peak < 0 ? -peak : peak) >= threshold) {
    s_dev->state->shake_holdoff_us = now + QMI8658_SHAKE_HOLDOFF_US;
    pbl_accel_report_event(&s_dev->accel, &(const struct pbl_accel_event){
                                            .type = PBL_ACCEL_EVENT_SHAKE,
                                            .axis = (IMUCoordinateAxis)axis,
                                            .direction = peak < 0 ? -1 : 1
                                          });
  }
}

static void prv_deliver_sample(void) {
  // Only this callback touches the shake filter and delivers manager callbacks.
  // The worker cannot overwrite the slot until delivery has finished.
  pbl_irq_lock();
  const bool valid =
      s_dev->state->active && s_dev->state->sample_generation == s_dev->state->generation;
  const AccelDriverSample sample = s_dev->state->pending_sample;
  pbl_irq_unlock();
  if (!valid) {
    prv_finish_poll();
    return;
  }
  if (sample.timestamp_us < s_dev->state->startup_us) {
    s_dev->state->startup_us = sample.timestamp_us;
    s_dev->state->shake_baseline_valid = false;
  }
  if (sample.timestamp_us - s_dev->state->startup_us < QMI8658_STARTUP_SETTLE_MS * 1000u) {
    prv_finish_poll();
    return;
  }
  qmi_profile_delivery(1u, prv_time_us(), sample.timestamp_us);
  s_dev->state->last_sample = sample;
  s_dev->state->last_sample_valid = true;
  prv_detect_shake(&sample);
  if (s_dev->state->num_samples != 0u) {
    pbl_accel_report_sample(&s_dev->accel, &sample);
  }
  prv_finish_poll();
}

// I2C waits belong to the sensor worker, never KernelBG or NewTimers.
static void prv_acquire_sample(void) {
  qmi_profile_acquire_start();
  pbl_irq_lock();
  const bool active = s_dev->state->initialized && s_dev->state->active;
  const uint32_t generation = s_dev->state->generation;
  pbl_irq_unlock();
  AccelDriverSample sample;
  if (!active || prv_read_sample(&sample) != QMI8658_SAMPLE_READY) {
    qmi_profile_acquire_end(0u);
    prv_finish_poll();
    return;
  }
  qmi_profile_acquire_end(1u);
  pbl_irq_lock();
  const bool valid = s_dev->state->active && generation == s_dev->state->generation;
  if (valid) {
    s_dev->state->pending_sample = sample;
    s_dev->state->sample_generation = generation;
  }
  pbl_irq_unlock();
  if (!valid || !prv_queue_work(prv_deliver_sample)) {
    prv_finish_poll();
  }
}

static void prv_poll_entry(void *context) {
  (void)context;
  while (true) {
    if (pbl_sem_take(&s_dev->state->poll_signal, PBL_FOREVER) == 0) {
      prv_acquire_sample();
    }
  }
}

static void prv_timer_callback(void *context) {
  qmi_profile_watchdog();
  (void)context;
  pbl_irq_lock();
  const bool schedule = s_dev->state->active && !s_dev->state->poll_pending;
  if (schedule) {
    s_dev->state->poll_pending = true;
  }
  pbl_irq_unlock();
  if (schedule) {
    pbl_sem_give(&s_dev->state->poll_signal);
  }
}

static bool prv_update_power(void) {
  const bool active = s_dev->state->num_samples != 0u || s_dev->state->shake_enabled;
  if (active == s_dev->state->active) {
    return true;
  }
  prv_invalidate_poll();
  if (!prv_write(QMI8658_CTRL7, active ? QMI8658_ACCEL_ENABLE : 0u)) {
    PBL_LOG_ERR("Could not %s QMI8658", active ? "enable" : "disable");
    return false;
  }
  pbl_irq_lock();
  s_dev->state->active = active;
  ++s_dev->state->generation;
  pbl_irq_unlock();
  s_dev->state->last_sample_valid = false;
  s_dev->state->shake_baseline_valid = false;
  if (active) {
    s_dev->state->startup_us = prv_time_us();
    const uint32_t period_ms = (s_dev->state->interval_us + 999u) / 1000u;
    (void)new_timer_start(s_dev->state->timer, period_ms, prv_timer_callback, NULL,
                          TIMER_START_FLAG_REPEATING);
  } else {
    (void)new_timer_stop(s_dev->state->timer);
  }
  return true;
}

static int prv_init(const struct pbl_accel *accel) {
  if (s_dev && &s_dev->accel != accel)
    return -ENOTSUP;
  s_dev = container_of(accel, const struct pbl_qmi8658, accel);
  s_dev->state->initialized = false;
  s_dev->state->timer = TIMER_INVALID_ID;
  s_dev->state->interval_us = s_dev->state->requested_interval_us = QMI8658_DEFAULT_INTERVAL_US;
  s_dev->state->shake_sensitivity_percent = 50;
  pbl_mutex_init(&s_dev->state->io_lock);
  pbl_sem_init(&s_dev->state->poll_signal, 0, 1);
#ifdef CONFIG_T5AI_ACCEL_DISABLED
  // AP-only resets can leave the sensor powered from the previous firmware.
  if (!prv_write(QMI8658_CTRL7, 0u)) {
    PBL_LOG_ERR("Could not power down QMI8658 for diagnostic");
  } else {
    PBL_LOG_DBG("QMI8658 disabled for diagnostic: no worker, polling or sample delivery");
  }
  return 0;
#endif
  if (!prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND)) {
    PBL_LOG_ERR("QMI8658 reset failed");
    return -EIO;
  }
  pbl_sleep_ms(QMI8658_RESET_SETTLE_MS);

  uint8_t id = 0xffu;
  if (!prv_read(QMI8658_WHO_AM_I, &id, 1) || id != QMI8658_ID) {
    PBL_LOG_ERR("QMI8658 not detected (id 0x%02x)", id);
    return -EIO;
  }
  if (!prv_write(QMI8658_CTRL1, QMI8658_CTRL1_INIT) ||
      !prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_rates[0].odr) ||
      !prv_write(QMI8658_CTRL7, 0u)) {
    PBL_LOG_ERR("QMI8658 initialization failed");
    return -EIO;
  }
  s_dev->state->timer = new_timer_create();
  if (s_dev->state->timer == TIMER_INVALID_ID) {
    PBL_LOG_ERR("Could not create QMI8658 poll timer");
    return -ENOMEM;
  }
  const struct pbl_thread_attr attr = {
    .name = "QMI8658",
    .entry = prv_poll_entry,
    // Acquire ahead of apps; blocking I2C remains below NewTimers.
    .prio = PBL_PRIO_MAX - 1,
    .privileged = true,
    .stack = s_dev->state->poll_stack,
    .stack_size = sizeof(s_dev->state->poll_stack),
  };
  if (pbl_thread_create(&s_dev->state->poll_thread, &attr) != 0) {
    PBL_LOG_ERR("Could not create QMI8658 worker");
    new_timer_delete(s_dev->state->timer);
    return -EIO;
  }
  s_dev->state->initialized = true;
  return 0;
}

static int prv_set_rotated(const struct pbl_accel *accel, bool rotated) {
  pbl_irq_lock();
  if (s_dev->state->rotated != rotated) {
    ++s_dev->state->generation;
    s_dev->state->shake_baseline_valid = false;
    s_dev->state->last_sample_valid = false;
  }
  s_dev->state->rotated = rotated;
  pbl_irq_unlock();
  return 0;
}

static int prv_set_sampling_interval(const struct pbl_accel *accel, uint32_t interval_us,
                                     uint32_t *actual_us) {
  int ret = 0;
  const uint32_t requested_interval_us = interval_us;
  if (s_dev->state->shake_enabled && interval_us > QMI8658_SHAKE_INTERVAL_US) {
    interval_us = QMI8658_SHAKE_INTERVAL_US;
  }
  const QMI8658Rate *rate = &s_rates[0];
  for (size_t i = 1; i < sizeof(s_rates) / sizeof(s_rates[0]); ++i) {
    if (s_rates[i].interval_us <= interval_us) {
      rate = &s_rates[i];
    }
  }
  prv_invalidate_poll();
  if (s_dev->state->initialized && !prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | rate->odr)) {
    PBL_LOG_ERR("Could not set QMI8658 sampling rate");
    return -EIO;
  }
  prv_invalidate_poll();
  s_dev->state->requested_interval_us = requested_interval_us;
  s_dev->state->interval_us = rate->interval_us;
  if (s_dev->state->timer != TIMER_INVALID_ID && s_dev->state->active) {
    const uint32_t period_ms = (s_dev->state->interval_us + 999u) / 1000u;
    (void)new_timer_start(s_dev->state->timer, period_ms, prv_timer_callback, NULL,
                          TIMER_START_FLAG_REPEATING);
  }
  *actual_us = s_dev->state->interval_us;
  return ret;
}

static uint32_t prv_get_sampling_interval(const struct pbl_accel *accel) {
  return s_dev->state->interval_us;
}

static int prv_set_num_samples(const struct pbl_accel *accel, uint32_t num_samples) {
  if (num_samples > 1)
    return -EINVAL;
  if (!s_dev->state->initialized || num_samples == s_dev->state->num_samples) {
    return s_dev->state->initialized ? 0 : -ENODEV;
  }
  int ret = 0;
  const uint32_t previous = s_dev->state->num_samples;
  s_dev->state->num_samples = num_samples;
  if (!prv_update_power()) {
    ret = -EIO;
    s_dev->state->num_samples = previous;
  }
  return ret;
}

static uint32_t prv_get_max_num_samples(const struct pbl_accel *accel) {
  return 1u;
}

static int prv_peek(const struct pbl_accel *accel, AccelDriverSample *data) {
  if (!data)
    return -EINVAL;
  if (!s_dev->state->initialized)
    return -ENODEV;
  if (s_dev->state->active && s_dev->state->last_sample_valid) {
    *data = s_dev->state->last_sample;
    return 0;
  }
  const bool temporary_enable = !s_dev->state->active;
  if (temporary_enable) {
    if (!prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE)) {
      return -EIO;
    }
    // The first conversions after power-up are saturated startup transients.
    pbl_sleep_ms(QMI8658_STARTUP_SETTLE_MS);
  }
  const bool result = prv_read_sample(data) == QMI8658_SAMPLE_READY;
  if (temporary_enable) {
    (void)prv_write(QMI8658_CTRL7, 0u);
  }
  if (result) {
    s_dev->state->last_sample = *data;
    s_dev->state->last_sample_valid = true;
  }
  return result ? 0 : -EIO;
}

static int prv_set_shake_detection(const struct pbl_accel *accel, bool on) {
  if (!s_dev->state->initialized || on == s_dev->state->shake_enabled) {
    return s_dev->state->initialized ? 0 : -ENODEV;
  }
  const uint32_t requested_interval_us = s_dev->state->requested_interval_us;
  s_dev->state->shake_enabled = on;
  s_dev->state->shake_baseline_valid = false;
  uint32_t interval_us;
  const int ret = prv_set_sampling_interval(accel, requested_interval_us, &interval_us);
  if (ret != 0 || (on && interval_us > QMI8658_SHAKE_INTERVAL_US) || !prv_update_power()) {
    s_dev->state->shake_enabled = !on;
    (void)prv_set_sampling_interval(accel, requested_interval_us, &interval_us);
    return -EIO;
  }
  return 0;
}

static int prv_set_shake_sensitivity_high(const struct pbl_accel *accel, bool sensitivity_high) {
  s_dev->state->shake_sensitivity_high = sensitivity_high;
  return 0;
}

static int prv_set_shake_sensitivity_percent(const struct pbl_accel *accel, uint8_t percent) {
  s_dev->state->shake_sensitivity_percent = percent > 100u ? 100u : percent;
  return 0;
}

static void prv_work_callback(void *data) {
  void (*work)(void) = data;
  pbl_accel_lock(&s_dev->accel);
  work();
  pbl_accel_unlock(&s_dev->accel);
}
static bool prv_queue_work(void (*work)(void)) {
  return new_timer_add_work_callback(prv_work_callback, work);
}
const struct pbl_accel_ops pbl_qmi8658_ops = {
  .init = prv_init,
  .set_rotated = prv_set_rotated,
  .set_sampling_interval = prv_set_sampling_interval,
  .get_sampling_interval = prv_get_sampling_interval,
  .set_num_samples = prv_set_num_samples,
  .get_max_num_samples = prv_get_max_num_samples,
  .peek = prv_peek,
  .set_shake_detection = prv_set_shake_detection,
  .set_shake_sensitivity_high = prv_set_shake_sensitivity_high,
  .set_shake_sensitivity_percent = prv_set_shake_sensitivity_percent,
};
