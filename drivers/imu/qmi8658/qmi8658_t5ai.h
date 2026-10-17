/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/drivers/accel.h>

// Hardware implementation shared by the FIFO and legacy acquisition paths.
void accel_init(void);
bool qmi8658_is_initialized(void);
void accel_set_rotated(bool rotated);
uint32_t accel_set_sampling_interval(uint32_t interval_us);
uint32_t accel_get_sampling_interval(void);
void accel_set_num_samples(uint32_t num_samples);
uint32_t accel_get_max_num_samples(void);
int accel_peek(AccelDriverSample *sample);
void accel_enable_shake_detection(bool on);
bool accel_get_shake_detection_enabled(void);
void accel_enable_double_tap_detection(bool on);
bool accel_get_double_tap_detection_enabled(void);
void accel_set_shake_sensitivity_high(bool high);
void accel_set_shake_sensitivity_percent(uint8_t percent);

void accel_cb_new_sample(const AccelDriverSample *sample);
void accel_cb_new_samples(const AccelRawBatch *batch);
void accel_cb_shake_detected(IMUCoordinateAxis axis, int32_t direction);
bool accel_offload_work(void (*callback)(void));
