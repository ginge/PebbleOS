/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "hardware_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_src[TUYA_LOGICAL_SIZE * TUYA_LOGICAL_SIZE];
static uint8_t s_dst[TUYA_FRAME_BYTES + 2];

static void test_pixels(void) {
  memset(s_src, 0xff, sizeof(s_src));
  memset(s_dst, 0x5a, sizeof(s_dst));
  assert(!tuya_expand_frame(NULL, sizeof(s_src), s_dst, sizeof(s_dst), true));
  assert(!tuya_expand_frame(s_src, sizeof(s_src) - 1, s_dst, sizeof(s_dst), true));
  assert(!tuya_expand_frame(s_src, sizeof(s_src), s_dst, TUYA_FRAME_BYTES - 1, true));
  assert(s_dst[0] == 0x5a);
  assert(tuya_expand_frame(s_src, sizeof(s_src), s_dst, TUYA_FRAME_BYTES, true));
  for (size_t i = 0; i < TUYA_FRAME_BYTES; ++i) {
    assert(s_dst[i] == 0xff);
  }
  assert(s_dst[TUYA_FRAME_BYTES] == 0x5a);
  const uint8_t colors[] = {0xc0, 0xf0, 0xcc, 0xc3, 0xff, 0xd5, 0xea};
  const uint16_t expected[] = {0, 0xf800, 0x07e0, 0x001f, 0xffff, 0x52aa, 0xad55};
  for (unsigned c = 0; c < sizeof(colors); ++c) {
    memset(s_src, colors[c], sizeof(s_src));
    assert(tuya_expand_frame(s_src, sizeof(s_src), s_dst, sizeof(s_dst), true));
    assert(s_dst[0] == (expected[c] >> 8));
    assert(s_dst[1] == (expected[c] & 0xff));
    assert(tuya_expand_frame(s_src, sizeof(s_src), s_dst, sizeof(s_dst), false));
    assert(s_dst[0] == (expected[c] & 0xff));
    assert(s_dst[1] == (expected[c] >> 8));
  }
  memset(s_src, 0xc0, sizeof(s_src));
  s_src[sizeof(s_src) - 1] = 0xf0;
  assert(tuya_expand_frame(s_src, sizeof(s_src), s_dst, sizeof(s_dst), true));
  assert(s_dst[0] == 0);
  assert(s_dst[TUYA_FRAME_BYTES - 2] == 0xf8);
  assert(s_dst[TUYA_FRAME_BYTES - 1] == 0);
}

static void test_touch(void) {
  uint16_t x = 99, y = 99;
  assert(tuya_map_touch(0, 0, &x, &y));
  assert(x == 259 && y == 259);
  assert(tuya_map_touch(465, 465, &x, &y));
  assert(x == 0 && y == 0);
  assert(!tuya_map_touch(466, 0, &x, &y));
  assert(!tuya_map_touch(0, 65535, &x, &y));
  assert(x == 0 && y == 0);
  assert(!tuya_map_touch(0, 0, NULL, &y));
  for (uint16_t i = 0; i < 466; ++i) {
    assert(tuya_map_touch(i, i, &x, &y));
    assert(x < 260 && x == y);
  }
}

static void test_rtc(void) {
  uint8_t regs[] = {0x59, 0x58, 0x23, 0x29, 0x04, 0x02, 0x24};
  TuyaDateTime time = {0};
  assert(tuya_decode_rtc(regs, &time));
  assert(time.year == 2024 && time.month == 2 && time.day == 29);
  assert(time.hour == 23 && time.minute == 58 && time.second == 59);
  regs[6] = 0x23;
  assert(!tuya_decode_rtc(regs, &time));
  regs[6] = 0x24;
  regs[0] |= 0x80;
  assert(!tuya_decode_rtc(regs, &time));
  regs[0] = 0x6a;
  assert(!tuya_decode_rtc(regs, &time));
  regs[0] = 0x59;
  regs[5] = 0x13;
  assert(!tuya_decode_rtc(regs, &time));
  regs[5] = 0x02;
  regs[4] = 7;
  assert(!tuya_decode_rtc(regs, &time));
  assert(time.year == 2024 && time.day == 29);
  assert(!tuya_decode_rtc(NULL, &time));
}

static void test_buttons(void) {
  TuyaButton b = {0};
  assert(!tuya_debounce(&b, true, 100));
  assert(!tuya_debounce(&b, false, 110));
  assert(!tuya_debounce(&b, true, 120));
  assert(!tuya_debounce(&b, true, 149));
  assert(tuya_debounce(&b, true, 150) && b.stable);
  assert(!tuya_debounce(&b, true, 180));
  assert(!tuya_debounce(&b, false, UINT32_MAX - 10));
  assert(tuya_debounce(&b, false, 20) && !b.stable);
}

int main(void) {
  test_pixels();
  test_touch();
  test_rtc();
  test_buttons();
  puts("hardware core: pixel, scaling, touch, RTC, debounce tests passed");
  return 0;
}
