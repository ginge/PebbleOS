/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUYA_PANEL_SIZE 466u
#define TUYA_LOGICAL_SIZE 260u
#define TUYA_FRAME_BYTES (TUYA_PANEL_SIZE * TUYA_PANEL_SIZE * 2u)

typedef struct {
  uint16_t year;
  uint8_t month, day, weekday, hour, minute, second;
} TuyaDateTime;

typedef struct {
  bool stable, candidate;
  uint32_t since_ms;
} TuyaButton;

/* Pebble's opaque framebuffer bytes use ARGB2222, not RGB332. */
bool tuya_expand_frame(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                       bool swap_bytes);
bool tuya_map_touch(uint16_t native_x, uint16_t native_y, uint16_t *x, uint16_t *y);
bool tuya_decode_rtc(const uint8_t registers[7], TuyaDateTime *time);
bool tuya_debounce(TuyaButton *button, bool pressed, uint32_t now_ms);
