/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "hardware_core.h"
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
