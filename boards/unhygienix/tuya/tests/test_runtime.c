/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "rtc_port.h"
#include "input_port.h"
#include "battery_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct Bus {
  uint8_t registers[18];
  unsigned calls, fail_at;
};
static int read_reg(void *context, uint8_t reg, uint8_t *data, size_t size) {
  struct Bus *bus = context;
  assert((size_t)reg + size <= sizeof(bus->registers));
  if (++bus->calls == bus->fail_at) {
    return -42;
  }
  memcpy(data, bus->registers + reg, size);
  return 0;
}
static int write_reg(void *context, uint8_t reg, const uint8_t *data, size_t size) {
  struct Bus *bus = context;
  assert((size_t)reg + size <= sizeof(bus->registers));
  if (++bus->calls == bus->fail_at) {
    if (size > 1) {
      bus->registers[reg] = data[0];
    } /* Torn I2C transfer. */
    return -42;
  }
  memcpy(bus->registers + reg, data, size);
  return 0;
}
static void test_rtc_transactions(void) {
  TuyaDateTime time = {
      .year = 2024, .month = 2, .day = 29, .weekday = 4, .hour = 13, .minute = 14, .second = 15};
  uint8_t encoded[7];
  assert(tuya_encode_rtc(&time, encoded));
  assert(encoded[2] == 0x13 && encoded[6] == 0x24);
  time.year = 2023;
  assert(!tuya_encode_rtc(&time, encoded));
  time.year = 2024;
  for (unsigned failure = 0; failure <= 4; ++failure) {
    struct Bus mock = {.fail_at = failure};
    mock.registers[0] = 0x03; /* Preserve CAP_SEL; replace 12-hour mode. */
    TuyaRtcBus bus = {.context = &mock, .read = read_reg, .write = write_reg};
    int result = tuya_rtc_set(&bus, &time);
    if (!failure) {
      assert(result == 0 && mock.calls == 4);
      assert(mock.registers[0] == 1);
      assert(memcmp(mock.registers + 4, encoded, 7) == 0);
    } else {
      assert(result == -42 && mock.calls == failure);
      if (failure >= 3) {
        assert(mock.registers[0] & 0x20);
      }
    }
  }
  struct Bus mock = {0};
  TuyaRtcBus bus = {.context = &mock, .read = read_reg, .write = write_reg};
  time.year = 2100;
  assert(tuya_rtc_set(&bus, &time) != 0 && mock.calls == 0);
  for (unsigned failure = 0; failure <= 5; ++failure) {
    memset(&mock, 0, sizeof(mock));
    mock.fail_at = failure;
    mock.registers[1] = 0x07;
    int result = tuya_rtc_daily_alarm(&bus, 12, 34, 56);
    if (!failure) {
      assert(result == 0);
      assert(mock.registers[1] == 0x8f);
      assert(mock.registers[0x0b] == 0x56 && mock.registers[0x0c] == 0x34);
      assert(mock.registers[0x0e] == 0x80 && mock.registers[0x0f] == 0x80);
      assert(tuya_rtc_alarm_clear(&bus) == 0 && mock.registers[1] == 0x0f);
    } else {
      assert(result == -42 && mock.calls == failure);
      if (failure >= 4) {
        assert(!(mock.registers[1] & 0x80));
      }
    }
  }
}

static unsigned s_down[NUM_BUTTONS], s_up[NUM_BUTTONS], s_touch[3];
static TouchEvent s_last_touch;
static void on_button(void *context, ButtonId id, bool down) {
  (void)context;
  assert(id < NUM_BUTTONS);
  if (down) {
    ++s_down[id];
  } else {
    ++s_up[id];
  }
}
static void on_touch(void *context, const TouchEvent *event) {
  (void)context;
  assert(event->type <= TouchEvent_PositionUpdate);
  ++s_touch[event->type];
  s_last_touch = *event;
}
static void test_inputs(void) {
  TuyaInputState state = {0};
  TuyaInputSink sink = {.button = on_button, .touch = on_touch, .swipe_buttons = true};
  assert(tuya_input_process(&state, &sink, true, 100, 150, true, false, UINT32_MAX - 50));
  assert(s_down[BUTTON_ID_BACK] == 1 && s_touch[TouchEvent_Touchdown] == 1);
  assert(tuya_input_process(&state, &sink, true, 100, 50, true, false, 5));
  assert(tuya_input_process(&state, &sink, false, 0, 0, false, true, 20));
  assert(s_down[BUTTON_ID_UP] == 1 && s_up[BUTTON_ID_BACK] == 1);
  assert(s_down[BUTTON_ID_SELECT] == 1);
  assert(s_last_touch.type == TouchEvent_Liftoff && s_last_touch.x == 100 && s_last_touch.y == 50);
  assert(s_last_touch.non_navigational);
  assert(tuya_input_process(&state, &sink, false, 0, 0, false, false, 40));
  assert(s_up[BUTTON_ID_UP] == 1 && s_up[BUTTON_ID_SELECT] == 1);
  TuyaInputState snapshot = state;
  assert(!tuya_input_process(&state, &sink, true, 260, 10, false, false, 60));
  assert(memcmp(&state, &snapshot, sizeof(state)) == 0);
  sink.swipe_buttons = false;
  assert(tuya_input_process(&state, &sink, true, 100, 10, false, false, 100));
  assert(!s_last_touch.non_navigational);
  assert(tuya_input_process(&state, &sink, true, 100, 200, false, false, 200));
  assert(tuya_input_process(&state, &sink, false, 0, 0, false, false, 300));
  assert(s_down[BUTTON_ID_DOWN] == 0);
  sink.swipe_buttons = true;
  assert(tuya_input_process(&state, &sink, true, 100, 10, false, false, 400));
  assert(tuya_input_process(&state, &sink, true, 100, 50, false, false, 500));
  assert(tuya_input_process(&state, &sink, false, 0, 0, false, false, 600));
  assert(s_down[BUTTON_ID_DOWN] == 1);
  assert(tuya_input_process(&state, &sink, false, 0, 0, false, false, 620));
  assert(s_up[BUTTON_ID_DOWN] == 1);
  /* A long drag is not a navigation click. */
  assert(tuya_input_process(&state, &sink, true, 100, 10, false, false, 700));
  assert(tuya_input_process(&state, &sink, true, 100, 200, false, false, 1600));
  assert(tuya_input_process(&state, &sink, false, 0, 0, false, false, 1701));
  assert(s_down[BUTTON_ID_DOWN] == 1);
}
static void test_battery(void) {
  TuyaBatteryCalibration cal = {.raw_low = 1000, .raw_high = 2000, .mv_low = 3000, .mv_high = 4200};
  uint16_t mv = 99;
  assert(tuya_battery_millivolts(1500, &cal, &mv) && mv == 3600);
  assert(!tuya_battery_millivolts(-1, &cal, &mv) && mv == 3600);
  assert(!tuya_battery_millivolts(65535, &cal, &mv));
  assert(!tuya_battery_millivolts(1500, NULL, &mv));
  cal.raw_high = 1000;
  assert(!tuya_battery_millivolts(1500, &cal, &mv));
}
int main(void) {
  test_rtc_transactions();
  test_inputs();
  test_battery();
  puts("runtime: RTC failure injection, input events, battery calibration passed");
  return 0;
}
