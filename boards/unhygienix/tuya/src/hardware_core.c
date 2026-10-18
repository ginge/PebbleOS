/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "hardware_core.h"

bool tuya_expand_frame(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                       bool swap_bytes) {
  if (!src || !dst || src_size < TUYA_LOGICAL_SIZE * TUYA_LOGICAL_SIZE ||
      dst_size < TUYA_FRAME_BYTES) {
    return false;
  }
  for (uint32_t y = 0; y < TUYA_PANEL_SIZE; ++y) {
    uint32_t sy = y * TUYA_LOGICAL_SIZE / TUYA_PANEL_SIZE;
    for (uint32_t x = 0; x < TUYA_PANEL_SIZE; ++x) {
      uint32_t sx = x * TUYA_LOGICAL_SIZE / TUYA_PANEL_SIZE;
      uint8_t p = src[sy * TUYA_LOGICAL_SIZE + sx];
      static const uint8_t levels[4] = {0, 10, 21, 31};
      uint16_t r = levels[(p >> 4) & 3];
      uint16_t g = ((p >> 2) & 3) * 21;
      uint16_t b = levels[p & 3];
      uint16_t rgb = (r << 11) | (g << 5) | b;
      size_t offset = (y * TUYA_PANEL_SIZE + x) * 2;
      dst[offset] = swap_bytes ? rgb >> 8 : rgb & 0xff;
      dst[offset + 1] = swap_bytes ? rgb & 0xff : rgb >> 8;
    }
  }
  return true;
}

bool tuya_map_touch(uint16_t native_x, uint16_t native_y, uint16_t *x, uint16_t *y) {
  if (!x || !y || native_x >= TUYA_PANEL_SIZE || native_y >= TUYA_PANEL_SIZE) {
    return false;
  }
  /* The Waveshare sensor axes are mirrored relative to the unrotated panel. */
  *x = (TUYA_PANEL_SIZE - 1 - native_x) * TUYA_LOGICAL_SIZE / TUYA_PANEL_SIZE;
  *y = (TUYA_PANEL_SIZE - 1 - native_y) * TUYA_LOGICAL_SIZE / TUYA_PANEL_SIZE;
  return true;
}

static bool prv_bcd(uint8_t raw, uint8_t min, uint8_t max, uint8_t *value) {
  if ((raw & 15) > 9 || (raw >> 4) > 9) {
    return false;
  }
  *value = (raw >> 4) * 10 + (raw & 15);
  return *value >= min && *value <= max;
}

bool tuya_decode_rtc(const uint8_t registers[7], TuyaDateTime *time) {
  if (!registers || !time || (registers[0] & 0x80)) {
    return false;
  }
  TuyaDateTime decoded = {0};
  uint8_t year;
  if (!prv_bcd(registers[0] & 0x7f, 0, 59, &decoded.second) ||
      !prv_bcd(registers[1] & 0x7f, 0, 59, &decoded.minute) ||
      !prv_bcd(registers[2] & 0x3f, 0, 23, &decoded.hour) ||
      !prv_bcd(registers[3] & 0x3f, 1, 31, &decoded.day) ||
      !prv_bcd(registers[5] & 0x1f, 1, 12, &decoded.month) ||
      !prv_bcd(registers[6], 0, 99, &year) || (registers[4] & 7) > 6) {
    return false;
  }
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t max_day = days[decoded.month - 1];
  decoded.year = 2000 + year;
  if (decoded.month == 2 && year % 4 == 0) {
    ++max_day;
  }
  if (decoded.day > max_day) {
    return false;
  }
  decoded.weekday = registers[4] & 7;
  *time = decoded;
  return true;
}

bool tuya_debounce(TuyaButton *button, bool pressed, uint32_t now_ms) {
  if (pressed != button->candidate) {
    button->candidate = pressed;
    button->since_ms = now_ms;
  }
  if (button->candidate != button->stable && (uint32_t)(now_ms - button->since_ms) >= 30) {
    button->stable = button->candidate;
    return true;
  }
  return false;
}
