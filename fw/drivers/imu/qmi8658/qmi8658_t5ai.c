/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include <pbl/drivers/accel.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include "kernel/util/sleep.h"
#include "pbl/services/new_timer/new_timer.h"
#include "system/status_codes.h"
#include <pbl/logging/logging.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

PBL_LOG_MODULE_DEFINE(driver_accel_qmi8658, CONFIG_DRIVER_IMU_LOG_LEVEL);

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

static TimerID s_timer = TIMER_INVALID_ID;
static uint32_t s_interval_us = QMI8658_DEFAULT_INTERVAL_US;
static uint32_t s_requested_interval_us = QMI8658_DEFAULT_INTERVAL_US;
static uint32_t s_num_samples;
static bool s_shake_enabled;
static bool s_active;
static bool s_shake_sensitivity_high;
static uint8_t s_shake_sensitivity_percent = 50u;
static bool s_shake_baseline_valid;
static int32_t s_shake_baseline[3];
static uint64_t s_shake_previous_us;
static uint64_t s_shake_holdoff_us;
static uint64_t s_startup_us;
static bool s_initialized;
static bool s_rotated;
static AccelDriverSample s_last_sample;
static bool s_last_sample_valid;
// One acquisition/delivery may be outstanding. Timer ticks while it is pending
// are dropped rather than building a backlog on the sensor worker or timer service.
static bool s_poll_pending;
static uint32_t s_generation;
static uint32_t s_sample_generation;
static AccelDriverSample s_pending_sample;
static PBL_SEM_DEFINE(s_poll_signal, 0, 1);
static PBL_MUTEX_DEFINE(s_io_lock);
static struct pbl_thread s_poll_thread;
PBL_THREAD_STACK_DEFINE(s_poll_stack, 1536);

static void prv_invalidate_poll(void) {
  pbl_irq_lock();
  ++s_generation;
  pbl_irq_unlock();
}

static void prv_finish_poll(void) {
  pbl_irq_lock();
  s_poll_pending = false;
  pbl_irq_unlock();
}

static bool prv_read(uint8_t reg, uint8_t *data, size_t length) {
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  i2c_use(I2C_QMI8658);
  bool result = i2c_write_block(I2C_QMI8658, 1, &reg);
  if (result) {
    result = i2c_read_block(I2C_QMI8658, length, data);
  }
  i2c_release(I2C_QMI8658);
  pbl_mutex_unlock(&s_io_lock);
  return result;
}

static bool prv_write(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  (void)pbl_mutex_lock(&s_io_lock, PBL_FOREVER);
  i2c_use(I2C_QMI8658);
  const bool result = i2c_write_block(I2C_QMI8658, sizeof(data), data);
  i2c_release(I2C_QMI8658);
  pbl_mutex_unlock(&s_io_lock);
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
  if (s_rotated) {
    sample->x = -sample->x;
    sample->y = -sample->y;
  }
  sample->timestamp_us = prv_time_us();
  return QMI8658_SAMPLE_READY;
}

// Remove gravity with a low-pass baseline before comparing motion on each axis.
static void prv_detect_shake(const AccelDriverSample *sample) {
  if (!s_shake_enabled) {
    return;
  }
  const int32_t values[] = {sample->x, sample->y, sample->z};
  const uint64_t now = sample->timestamp_us;
  if (!s_shake_baseline_valid || now < s_shake_previous_us ||
      now - s_shake_previous_us > QMI8658_SHAKE_HOLDOFF_US) {
    for (size_t i = 0; i < 3; ++i) {
      s_shake_baseline[i] = values[i];
    }
    s_shake_baseline_valid = true;
    s_shake_holdoff_us = now;
    s_shake_previous_us = now;
    return;
  }
  s_shake_previous_us = now;
  const uint32_t threshold =
      s_shake_sensitivity_high
          ? QMI8658_SHAKE_MIN_MG
          : QMI8658_SHAKE_MAX_MG -
                s_shake_sensitivity_percent * (QMI8658_SHAKE_MAX_MG - QMI8658_SHAKE_MIN_MG) / 100u;
  int32_t peak = 0;
  size_t axis = 0;
  for (size_t i = 0; i < 3; ++i) {
    const int32_t delta = values[i] - s_shake_baseline[i];
    if ((delta < 0 ? -delta : delta) > (peak < 0 ? -peak : peak)) {
      peak = delta;
      axis = i;
    }
    s_shake_baseline[i] += delta / 8;
  }
  if (now >= s_shake_holdoff_us && (uint32_t)(peak < 0 ? -peak : peak) >= threshold) {
    s_shake_holdoff_us = now + QMI8658_SHAKE_HOLDOFF_US;
    accel_cb_shake_detected((IMUCoordinateAxis)axis, peak < 0 ? -1 : 1);
  }
}

static void prv_deliver_sample(void) {
  // Only this callback touches the shake filter and delivers manager callbacks.
  // The worker cannot overwrite the slot until delivery has finished.
  pbl_irq_lock();
  const bool valid = s_active && s_sample_generation == s_generation;
  const AccelDriverSample sample = s_pending_sample;
  pbl_irq_unlock();
  if (!valid) {
    prv_finish_poll();
    return;
  }
  if (sample.timestamp_us < s_startup_us) {
    s_startup_us = sample.timestamp_us;
    s_shake_baseline_valid = false;
  }
  if (sample.timestamp_us - s_startup_us < QMI8658_STARTUP_SETTLE_MS * 1000u) {
    prv_finish_poll();
    return;
  }
  s_last_sample = sample;
  s_last_sample_valid = true;
  prv_detect_shake(&sample);
  if (s_num_samples != 0u) {
    accel_cb_new_sample(&sample);
  }
  prv_finish_poll();
}

// I2C waits belong to the sensor worker, never KernelBG or NewTimers.
static void prv_acquire_sample(void) {
  pbl_irq_lock();
  const bool active = s_initialized && s_active;
  const uint32_t generation = s_generation;
  pbl_irq_unlock();
  AccelDriverSample sample;
  if (!active || prv_read_sample(&sample) != QMI8658_SAMPLE_READY) {
    prv_finish_poll();
    return;
  }
  pbl_irq_lock();
  const bool valid = s_active && generation == s_generation;
  if (valid) {
    s_pending_sample = sample;
    s_sample_generation = generation;
  }
  pbl_irq_unlock();
  if (!valid || !accel_offload_work(prv_deliver_sample)) {
    prv_finish_poll();
  }
}

static void prv_poll_entry(void *context) {
  (void)context;
  while (true) {
    if (pbl_sem_take(&s_poll_signal, PBL_FOREVER) == 0) {
      prv_acquire_sample();
    }
  }
}

static void prv_timer_callback(void *context) {
  (void)context;
  pbl_irq_lock();
  const bool schedule = s_active && !s_poll_pending;
  if (schedule) {
    s_poll_pending = true;
  }
  pbl_irq_unlock();
  if (schedule) {
    pbl_sem_give(&s_poll_signal);
  }
}

static bool prv_update_power(void) {
  const bool active = s_num_samples != 0u || s_shake_enabled;
  if (active == s_active) {
    return true;
  }
  prv_invalidate_poll();
  if (!prv_write(QMI8658_CTRL7, active ? QMI8658_ACCEL_ENABLE : 0u)) {
    PBL_LOG_ERR("Could not %s QMI8658", active ? "enable" : "disable");
    return false;
  }
  pbl_irq_lock();
  s_active = active;
  ++s_generation;
  pbl_irq_unlock();
  s_last_sample_valid = false;
  s_shake_baseline_valid = false;
  if (active) {
    s_startup_us = prv_time_us();
    const uint32_t period_ms = (s_interval_us + 999u) / 1000u;
    (void)new_timer_start(s_timer, period_ms, prv_timer_callback, NULL, TIMER_START_FLAG_REPEATING);
  } else {
    (void)new_timer_stop(s_timer);
  }
  return true;
}

void accel_init(void) {
#ifdef CONFIG_T5AI_ACCEL_DISABLED
  // AP-only resets can leave the sensor powered from the previous firmware.
  if (!prv_write(QMI8658_CTRL7, 0u)) {
    PBL_LOG_ERR("Could not power down QMI8658 for diagnostic");
  } else {
    PBL_LOG_DBG("QMI8658 disabled for diagnostic: no worker, polling or sample delivery");
  }
  return;
#endif
  if (!prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND)) {
    PBL_LOG_ERR("QMI8658 reset failed");
    return;
  }
  psleep(QMI8658_RESET_SETTLE_MS);

  uint8_t id = 0xffu;
  if (!prv_read(QMI8658_WHO_AM_I, &id, 1) || id != QMI8658_ID) {
    PBL_LOG_ERR("QMI8658 not detected (id 0x%02x)", id);
    return;
  }
  if (!prv_write(QMI8658_CTRL1, QMI8658_CTRL1_INIT) ||
      !prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | s_rates[0].odr) ||
      !prv_write(QMI8658_CTRL7, 0u)) {
    PBL_LOG_ERR("QMI8658 initialization failed");
    return;
  }
  s_timer = new_timer_create();
  if (s_timer == TIMER_INVALID_ID) {
    PBL_LOG_ERR("Could not create QMI8658 poll timer");
    return;
  }
  const struct pbl_thread_attr attr = {
      .name = "QMI8658",
      .entry = prv_poll_entry,
      // Acquire ahead of apps; blocking I2C remains below NewTimers.
      .prio = PBL_PRIO_MAX - 1,
      .privileged = true,
      .stack = s_poll_stack,
      .stack_size = sizeof(s_poll_stack),
  };
  if (pbl_thread_create(&s_poll_thread, &attr) != 0) {
    PBL_LOG_ERR("Could not create QMI8658 worker");
    return;
  }
  s_initialized = true;
}

void accel_set_rotated(bool rotated) {
  pbl_irq_lock();
  if (s_rotated != rotated) {
    ++s_generation;
    s_shake_baseline_valid = false;
    s_last_sample_valid = false;
  }
  s_rotated = rotated;
  pbl_irq_unlock();
}

uint32_t accel_set_sampling_interval(uint32_t interval_us) {
  const uint32_t requested_interval_us = interval_us;
  if (s_shake_enabled && interval_us > QMI8658_SHAKE_INTERVAL_US) {
    interval_us = QMI8658_SHAKE_INTERVAL_US;
  }
  const QMI8658Rate *rate = &s_rates[0];
  for (size_t i = 1; i < sizeof(s_rates) / sizeof(s_rates[0]); ++i) {
    if (s_rates[i].interval_us <= interval_us) {
      rate = &s_rates[i];
    }
  }
  prv_invalidate_poll();
  if (s_initialized && !prv_write(QMI8658_CTRL2, QMI8658_ACCEL_RANGE_4G | rate->odr)) {
    PBL_LOG_ERR("Could not set QMI8658 sampling rate");
    return s_interval_us;
  }
  prv_invalidate_poll();
  s_requested_interval_us = requested_interval_us;
  s_interval_us = rate->interval_us;
  if (s_timer != TIMER_INVALID_ID && s_active) {
    const uint32_t period_ms = (s_interval_us + 999u) / 1000u;
    (void)new_timer_start(s_timer, period_ms, prv_timer_callback, NULL, TIMER_START_FLAG_REPEATING);
  }
  return s_interval_us;
}

uint32_t accel_get_sampling_interval(void) {
  return s_interval_us;
}

void accel_set_num_samples(uint32_t num_samples) {
  num_samples = num_samples != 0u;
  if (!s_initialized || num_samples == s_num_samples) {
    return;
  }
  const uint32_t previous = s_num_samples;
  s_num_samples = num_samples;
  if (!prv_update_power()) {
    s_num_samples = previous;
  }
}

uint32_t accel_get_max_num_samples(void) {
  return 1u;
}

int accel_peek(AccelDriverSample *data) {
  if (!s_initialized || data == NULL) {
    return E_ERROR;
  }
  if (s_active && s_last_sample_valid) {
    *data = s_last_sample;
    return S_SUCCESS;
  }
  const bool temporary_enable = !s_active;
  if (temporary_enable) {
    if (!prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE)) {
      return E_ERROR;
    }
    // The first conversions after power-up are saturated startup transients.
    psleep(QMI8658_STARTUP_SETTLE_MS);
  }
  const bool result = prv_read_sample(data) == QMI8658_SAMPLE_READY;
  if (temporary_enable) {
    (void)prv_write(QMI8658_CTRL7, 0u);
  }
  if (result) {
    s_last_sample = *data;
    s_last_sample_valid = true;
  }
  return result ? S_SUCCESS : E_ERROR;
}

void accel_enable_shake_detection(bool on) {
  if (!s_initialized || on == s_shake_enabled) {
    return;
  }
  const uint32_t requested_interval_us = s_requested_interval_us;
  s_shake_enabled = on;
  s_shake_baseline_valid = false;
  const uint32_t interval_us = accel_set_sampling_interval(requested_interval_us);
  if ((on && interval_us > QMI8658_SHAKE_INTERVAL_US) || !prv_update_power()) {
    s_shake_enabled = !on;
    (void)accel_set_sampling_interval(requested_interval_us);
  }
}

bool accel_get_shake_detection_enabled(void) {
  return s_shake_enabled;
}

void accel_enable_double_tap_detection(bool on) {
  (void)on;
}

bool accel_get_double_tap_detection_enabled(void) {
  return false;
}

void accel_set_shake_sensitivity_high(bool sensitivity_high) {
  s_shake_sensitivity_high = sensitivity_high;
}

void accel_set_shake_sensitivity_percent(uint8_t percent) {
  s_shake_sensitivity_percent = percent > 100u ? 100u : percent;
}
