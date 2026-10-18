/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int32_t raw_low, raw_high;
  uint16_t mv_low, mv_high;
} TuyaBatteryCalibration;

/* Two measured battery-terminal voltages; no borrowed calibration for a different board. */
bool tuya_battery_millivolts(int32_t raw, const TuyaBatteryCalibration *cal, uint16_t *mv);
