/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "battery_port.h"

bool tuya_battery_millivolts(int32_t raw, const TuyaBatteryCalibration *cal, uint16_t *mv) {
  if (!cal || !mv || raw < 0 || raw > 65535 || cal->raw_low < 0 || cal->raw_high > 65535 ||
      cal->raw_high <= cal->raw_low || cal->mv_low < 2000 || cal->mv_high > 5000 ||
      cal->mv_high <= cal->mv_low) {
    return false;
  }
  int64_t value = cal->mv_low + ((int64_t)raw - cal->raw_low) * (cal->mv_high - cal->mv_low) /
                                    (cal->raw_high - cal->raw_low);
  if (value < 2000 || value > 5000) {
    return false;
  }
  *mv = value;
  return true;
}
