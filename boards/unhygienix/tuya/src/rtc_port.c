/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "rtc_port.h"
#include <string.h>

static uint8_t prv_bcd(uint8_t value) {
  return (value / 10 << 4) | value % 10;
}
static bool prv_valid_bus(const TuyaRtcBus *bus) {
  return bus && bus->read && bus->write;
}

bool tuya_encode_rtc(const TuyaDateTime *time, uint8_t registers[7]) {
  if (!time || !registers || time->year < 2000 || time->year > 2099 || time->weekday > 6 ||
      time->month > 12 || time->day > 31 || time->hour > 23 || time->minute > 59 ||
      time->second > 59) {
    return false;
  }
  uint8_t data[] = {prv_bcd(time->second),     prv_bcd(time->minute), prv_bcd(time->hour),
                    prv_bcd(time->day),        time->weekday,         prv_bcd(time->month),
                    prv_bcd(time->year - 2000)};
  TuyaDateTime decoded;
  if (!tuya_decode_rtc(data, &decoded)) {
    return false;
  }
  memcpy(registers, data, sizeof(data));
  return true;
}

int tuya_rtc_set(const TuyaRtcBus *bus, const TuyaDateTime *time) {
  uint8_t data[7], control;
  if (!prv_valid_bus(bus) || !tuya_encode_rtc(time, data)) {
    return -1;
  }
  int result = bus->read(bus->context, 0, &control, 1);
  if (result) {
    return result;
  }
  uint8_t stopped = (control | 0x20) & ~0x02; /* STOP; use 24-hour format. */
  result = bus->write(bus->context, 0, &stopped, 1);
  if (result) {
    return result;
  }
  result = bus->write(bus->context, 4, data, sizeof(data));
  /* A failed transfer may have changed a prefix. Leave STOP asserted so
     rtc_read rejects the partial calendar; an explicit retry is required. */
  if (result) {
    return result;
  }
  uint8_t running = control & ~0x22;
  int resume_result = bus->write(bus->context, 0, &running, 1);
  return resume_result ? resume_result : result;
}

int tuya_rtc_daily_alarm(const TuyaRtcBus *bus, uint8_t hour, uint8_t minute, uint8_t second) {
  if (!prv_valid_bus(bus) || hour > 23 || minute > 59 || second > 59) {
    return -1;
  }
  uint8_t control1, control2;
  int result = bus->read(bus->context, 0, &control1, 1);
  if (result) {
    return result;
  }
  if (control1 & 0x22) {
    return -1;
  }
  result = bus->read(bus->context, 1, &control2, 1);
  if (result) {
    return result;
  }
  /* Clear AIE/AF, preserve TF by writing one, leave clock output unchanged. */
  uint8_t disabled = (control2 & ~0xc0) | 0x08;
  result = bus->write(bus->context, 1, &disabled, 1);
  if (result) {
    return result;
  }
  uint8_t alarm[] = {prv_bcd(second), prv_bcd(minute), prv_bcd(hour), 0x80, 0x80};
  result = bus->write(bus->context, 0x0b, alarm, sizeof(alarm));
  if (result) {
    return result;
  }
  uint8_t enabled = disabled | 0x80;
  return bus->write(bus->context, 1, &enabled, 1);
}

int tuya_rtc_alarm_clear(const TuyaRtcBus *bus) {
  if (!prv_valid_bus(bus)) {
    return -1;
  }
  uint8_t control;
  int result = bus->read(bus->context, 1, &control, 1);
  if (result) {
    return result;
  }
  control = (control & ~0xc0) | 0x08;
  return bus->write(bus->context, 1, &control, 1);
}
