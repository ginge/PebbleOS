/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

void touch_sensor_init(void);

//! Enable or disable touch sensor interrupts.
//! When disabled, no touch events will be processed.
//! @param enabled true to enable, false to disable
void touch_sensor_set_enabled(bool enabled);

#ifdef CONFIG_SOC_T5AI
typedef struct TouchSensorDiagnostics {
  bool enabled;
  bool finger_down;
  uint32_t polls;
  uint32_t read_failures;
  uint32_t ack_reports;
  uint32_t valid_reports;
  uint16_t contact_samples;
  uint16_t coordinate_changes;
  int16_t first_x;
  int16_t first_y;
  int16_t min_x;
  int16_t min_y;
  int16_t max_x;
  int16_t max_y;
  int16_t last_x;
  int16_t last_y;
  uint8_t last_report[10];
} TouchSensorDiagnostics;

//! Copy read-only CST92xx runtime counters for the bounded T5AI USART probe.
void touch_sensor_get_diagnostics(TouchSensorDiagnostics *diagnostics);
#endif
