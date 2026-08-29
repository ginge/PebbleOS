/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include <pbl/drivers/accel.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/rtc.h>
#include <pbl/os/tick.h>
#include "kernel/util/sleep.h"
#include "pbl/services/system_task.h"
#include "system/status_codes.h"
#include <pbl/logging/logging.h>

#include "FreeRTOS.h"
#include "timers.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

PBL_LOG_MODULE_DEFINE(driver_accel_qmi8658, CONFIG_DRIVER_IMU_LOG_LEVEL);

#define QMI8658_WHO_AM_I 0x00u
#define QMI8658_CTRL1 0x02u
#define QMI8658_CTRL2 0x03u
#define QMI8658_CTRL7 0x08u
#define QMI8658_AX_L 0x35u

#define QMI8658_ID 0x05u
#define QMI8658_CTRL1_INIT 0x60u
#define QMI8658_ACCEL_RANGE_4G 0x10u
#define QMI8658_ACCEL_ENABLE 0x01u
#define QMI8658_COUNTS_PER_G 8192

typedef struct {
  uint32_t interval_us;
  uint8_t odr;
} QMI8658Rate;

static const QMI8658Rate s_rates[] = {
    {8000u, 0x06u}, {16000u, 0x07u}, {32000u, 0x08u},
    {48000u, 0x0du}, {91000u, 0x0eu}, {333000u, 0x0fu},
};

static TimerHandle_t s_timer;
static uint32_t s_interval_us = 8000u;
static uint32_t s_num_samples;
static bool s_initialized;
static bool s_poll_pending;
static bool s_rotated;
static AccelDriverSample s_last_sample;
static bool s_last_sample_valid;

static bool prv_read(uint8_t reg, uint8_t *data, size_t length) {
  i2c_use(I2C_QMI8658);
  bool result = i2c_write_block(I2C_QMI8658, 1, &reg);
  if (result) {
    result = i2c_read_block(I2C_QMI8658, length, data);
  }
  i2c_release(I2C_QMI8658);
  return result;
}

static bool prv_write(uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  i2c_use(I2C_QMI8658);
  const bool result = i2c_write_block(I2C_QMI8658, sizeof(data), data);
  i2c_release(I2C_QMI8658);
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

static bool prv_read_sample(AccelDriverSample *sample) {
  uint8_t raw[6];
  if (!prv_read(QMI8658_AX_L, raw, sizeof(raw))) {
    return false;
  }
  sample->x = prv_to_mg(&raw[0]);
  sample->y = prv_to_mg(&raw[2]);
  sample->z = prv_to_mg(&raw[4]);
  if (s_rotated) {
    sample->x = -sample->x;
    sample->y = -sample->y;
  }
  sample->timestamp_us = prv_time_us();
  return true;
}

static void prv_poll(void *context) {
  (void)context;
  s_poll_pending = false;
  if (!s_initialized || s_num_samples == 0u) {
    return;
  }
  AccelDriverSample sample;
  if (!prv_read_sample(&sample)) {
    PBL_LOG_ERR("QMI8658 sample read failed");
    return;
  }
  s_last_sample = sample;
  s_last_sample_valid = true;
  accel_cb_new_sample(&sample);
}

static void prv_timer_callback(TimerHandle_t timer) {
  (void)timer;
  if (s_num_samples == 0u || s_poll_pending) {
    return;
  }
  s_poll_pending = true;
  if (!system_task_add_callback(prv_poll, NULL)) {
    s_poll_pending = false;
  }
}

void accel_init(void) {
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
  s_timer = xTimerCreate("qmi8658", milliseconds_to_ticks(8u), pdTRUE, NULL,
                         prv_timer_callback);
  if (s_timer == NULL) {
    PBL_LOG_ERR("Could not create QMI8658 poll timer");
    return;
  }
  s_initialized = true;
}

void accel_set_rotated(bool rotated) {
  s_rotated = rotated;
}

uint32_t accel_set_sampling_interval(uint32_t interval_us) {
  const QMI8658Rate *rate = &s_rates[0];
  for (size_t i = 1; i < sizeof(s_rates) / sizeof(s_rates[0]); ++i) {
    if (s_rates[i].interval_us <= interval_us) {
      rate = &s_rates[i];
    }
  }
  if (s_initialized && !prv_write(QMI8658_CTRL2,
                                  QMI8658_ACCEL_RANGE_4G | rate->odr)) {
    PBL_LOG_ERR("Could not set QMI8658 sampling rate");
    return s_interval_us;
  }
  s_interval_us = rate->interval_us;
  if (s_timer != NULL) {
    const uint32_t period_ms = (s_interval_us + 999u) / 1000u;
    (void)xTimerChangePeriod(s_timer, milliseconds_to_ticks(period_ms), 0);
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
  s_num_samples = num_samples;
  s_poll_pending = false;
  if (!prv_write(QMI8658_CTRL7, num_samples ? QMI8658_ACCEL_ENABLE : 0u)) {
    PBL_LOG_ERR("Could not %s QMI8658", num_samples ? "enable" : "disable");
  }
  if (num_samples) {
    (void)xTimerStart(s_timer, 0);
  } else {
    (void)xTimerStop(s_timer, 0);
  }
}

uint32_t accel_get_max_num_samples(void) {
  return 1u;
}

int accel_peek(AccelDriverSample *data) {
  if (!s_initialized || data == NULL) {
    return E_ERROR;
  }
  if (s_num_samples != 0u && s_last_sample_valid) {
    *data = s_last_sample;
    return S_SUCCESS;
  }
  const bool temporary_enable = s_num_samples == 0u;
  if (temporary_enable) {
    if (!prv_write(QMI8658_CTRL7, QMI8658_ACCEL_ENABLE)) {
      return E_ERROR;
    }
    psleep((s_interval_us + 999u) / 1000u);
  }
  const bool result = prv_read_sample(data);
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
  (void)on;
}

bool accel_get_shake_detection_enabled(void) {
  return false;
}

void accel_enable_double_tap_detection(bool on) {
  (void)on;
}

bool accel_get_double_tap_detection_enabled(void) {
  return false;
}

void accel_set_shake_sensitivity_high(bool sensitivity_high) {
  (void)sensitivity_high;
}

void accel_set_shake_sensitivity_percent(uint8_t percent) {
  (void)percent;
}
