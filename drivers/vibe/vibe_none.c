/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/vibe.h>



void vibe_init(void) {
}

void vibe_ctl(bool on) {
  (void)on;
}

void vibe_force_off(void) {
}

void vibe_set_strength(int8_t strength) {
  (void)strength;
}

int8_t vibe_get_braking_strength(void) {
  return VIBE_STRENGTH_OFF;
}

status_t vibe_calibrate(void) {
  return E_INVALID_OPERATION;
}

uint8_t vibe_get_calibration(void) {
  return 0xff;
}

void vibe_apply_calibration(uint8_t cali) {
  (void)cali;
}
