/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "hardware_core.h"
#include "battery_port.h"
#include "tuya_cloud_types.h"

typedef struct {
  bool touched;
  uint16_t x, y;
  bool back, select, charging;
} TuyaInputs;

/* Singleton backend. Call init once; all public operations run on one task. */
OPERATE_RET tuya_hardware_init(void);
OPERATE_RET tuya_hardware_present(const uint8_t *frame, size_t size);
OPERATE_RET tuya_hardware_inputs(TuyaInputs *inputs, uint32_t now_ms);
OPERATE_RET tuya_hardware_brightness(uint8_t percent);
OPERATE_RET tuya_hardware_rtc_read(TuyaDateTime *time);
OPERATE_RET tuya_hardware_accel_read(float *x_mg, float *y_mg, float *z_mg);

typedef struct {
  int32_t raw_peak;
  uint16_t millivolts;
  bool calibrated;
} TuyaBatteryReading;

/* These calls do not run automatically; RTC writes are explicit operations. */
OPERATE_RET tuya_hardware_rtc_set(const TuyaDateTime *time);
OPERATE_RET tuya_hardware_rtc_daily_alarm(uint8_t hour, uint8_t minute, uint8_t second);
OPERATE_RET tuya_hardware_rtc_alarm_clear(void);
OPERATE_RET tuya_hardware_battery_read(TuyaBatteryReading *reading,
                                       const TuyaBatteryCalibration *calibration);
