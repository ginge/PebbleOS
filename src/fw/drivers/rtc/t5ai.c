/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rtc.h>
#include <pbl/drivers/i2c.h>
#include <pbl/soc/t5ai/retained.h>

#include "board/board.h"
#include "system/rtc_registers.h"

#include <stddef.h>
#include <string.h>

#define T5AI_AON_RTC_BASE 0x44000200u
#define T5AI_AON_RTC_COUNT_LOW (*(volatile uint32_t *)(T5AI_AON_RTC_BASE + 0x0cu))
#define T5AI_AON_RTC_COUNT_HIGH (*(volatile uint32_t *)(T5AI_AON_RTC_BASE + 0x28u))

#define PCF85063_REG_CONTROL1 0x00u
#define PCF85063_REG_SECONDS 0x04u
#define PCF85063_CONTROL1_STOP (1u << 5)
#define PCF85063_CONTROL1_12_24 (1u << 1)
#define PCF85063_SECONDS_OS (1u << 7)

static RtcTicks s_alarm_start;
static bool s_external_rtc_ready;

static RtcTicks prv_raw_ticks(void) {
  uint32_t low;
  uint32_t high;
  do {
    low = T5AI_AON_RTC_COUNT_LOW;
    high = T5AI_AON_RTC_COUNT_HIGH;
  } while (low != T5AI_AON_RTC_COUNT_LOW ||
           high != T5AI_AON_RTC_COUNT_HIGH);
  return ((uint64_t)high << 32) | low;
}

static RtcTicks prv_base_ticks(void) {
  return ((uint64_t)t5ai_retained_read(T5AI_RTC_BASE_TICKS_HIGH) << 32) |
         t5ai_retained_read(T5AI_RTC_BASE_TICKS_LOW);
}

static void prv_set_retained_time(time_t time) {
  const RtcTicks ticks = prv_raw_ticks();
  t5ai_retained_write(CURRENT_TIME_REGISTER, (uint32_t)time);
  t5ai_retained_write(T5AI_RTC_BASE_TICKS_LOW, (uint32_t)ticks);
  t5ai_retained_write(T5AI_RTC_BASE_TICKS_HIGH, (uint32_t)(ticks >> 32));
}

static bool prv_decode_bcd(uint8_t raw, uint8_t minimum, uint8_t maximum,
                           uint8_t *value) {
  if ((raw & 0x0fu) > 9u || (raw >> 4) > 9u) {
    return false;
  }
  *value = (raw >> 4) * 10u + (raw & 0x0fu);
  return *value >= minimum && *value <= maximum;
}

static uint8_t prv_encode_bcd(unsigned value) {
  return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static bool prv_external_rtc_read(time_t *time) {
  uint8_t control;
  uint8_t registers[7];
  i2c_use(I2C_PCF85063);
  const bool read_control = i2c_read_register(I2C_PCF85063,
                                               PCF85063_REG_CONTROL1,
                                               &control);
  const bool read_time = read_control &&
      i2c_read_register_block(I2C_PCF85063, PCF85063_REG_SECONDS,
                              sizeof(registers), registers);
  i2c_release(I2C_PCF85063);

  if (!read_time || (control & (PCF85063_CONTROL1_STOP |
                                PCF85063_CONTROL1_12_24)) ||
      (registers[0] & PCF85063_SECONDS_OS)) {
    return false;
  }

  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t month;
  uint8_t year;
  const uint8_t weekday = registers[4] & 0x07u;
  if (!prv_decode_bcd(registers[0] & 0x7fu, 0, 59, &second) ||
      !prv_decode_bcd(registers[1] & 0x7fu, 0, 59, &minute) ||
      !prv_decode_bcd(registers[2] & 0x3fu, 0, 23, &hour) ||
      !prv_decode_bcd(registers[3] & 0x3fu, 1, 31, &day) ||
      weekday > 6u ||
      !prv_decode_bcd(registers[5] & 0x1fu, 1, 12, &month) ||
      !prv_decode_bcd(registers[6], 0, 37, &year)) {
    return false;
  }

  struct tm decoded = {
      .tm_sec = second,
      .tm_min = minute,
      .tm_hour = hour,
      .tm_mday = day,
      .tm_mon = month - 1,
      .tm_year = year + 100,
      .tm_isdst = 0,
  };
  const time_t result = mktime(&decoded);
  if (decoded.tm_sec != second || decoded.tm_min != minute ||
      decoded.tm_hour != hour || decoded.tm_mday != day ||
      decoded.tm_mon != month - 1 || decoded.tm_year != year + 100 ||
      decoded.tm_wday != weekday) {
    return false;
  }
  *time = result;
  return true;
}

static bool prv_external_rtc_write(time_t time) {
  struct tm encoded;
  gmtime_r(&time, &encoded);
  if (encoded.tm_year < 100 || encoded.tm_year > 137) {
    return false;
  }

  const uint8_t registers[7] = {
      prv_encode_bcd(encoded.tm_sec),
      prv_encode_bcd(encoded.tm_min),
      prv_encode_bcd(encoded.tm_hour),
      prv_encode_bcd(encoded.tm_mday),
      (uint8_t)encoded.tm_wday,
      prv_encode_bcd(encoded.tm_mon + 1),
      prv_encode_bcd(encoded.tm_year - 100),
  };

  uint8_t control;
  i2c_use(I2C_PCF85063);
  bool result = i2c_read_register(I2C_PCF85063, PCF85063_REG_CONTROL1,
                                  &control);
  if (result) {
    const uint8_t stopped = (control | PCF85063_CONTROL1_STOP) &
                            ~PCF85063_CONTROL1_12_24;
    result = i2c_write_register(I2C_PCF85063, PCF85063_REG_CONTROL1,
                                stopped);
    result = result && i2c_write_register_block(I2C_PCF85063,
                                                 PCF85063_REG_SECONDS,
                                                 sizeof(registers), registers);
    if (result) {
      const uint8_t running = control & ~(PCF85063_CONTROL1_STOP |
                                          PCF85063_CONTROL1_12_24);
      result = i2c_write_register(I2C_PCF85063, PCF85063_REG_CONTROL1,
                                  running);
    }
  }
  i2c_release(I2C_PCF85063);
  return result;
}

void rtc_init(void) {
  t5ai_retained_init();
}

void rtc_calibrate_frequency(uint32_t frequency) {
  (void)frequency;
}

void rtc_init_timers(void) {
  s_external_rtc_ready = true;
  time_t external_time;
  if (prv_external_rtc_read(&external_time)) {
    prv_set_retained_time(external_time);
  }
}

void rtc_set_time(time_t time) {
  prv_set_retained_time(time);
  if (s_external_rtc_ready) {
    (void)prv_external_rtc_write(time);
  }
}

void rtc_get_time_ms(time_t *out_seconds, uint16_t *out_ms) {
  const RtcTicks elapsed = prv_raw_ticks() - prv_base_ticks();
  *out_seconds = (time_t)t5ai_retained_read(CURRENT_TIME_REGISTER) +
                 (time_t)(elapsed / RTC_TICKS_HZ);
  *out_ms = (uint16_t)(((elapsed % RTC_TICKS_HZ) * 1000u) /
                       RTC_TICKS_HZ);
}

time_t rtc_get_time(void) {
  time_t seconds;
  uint16_t milliseconds;
  rtc_get_time_ms(&seconds, &milliseconds);
  return seconds;
}

RtcTicks rtc_get_ticks(void) {
  return prv_raw_ticks();
}

void rtc_set_time_tm(struct tm *time_tm) {
  rtc_set_time(mktime(time_tm));
}

void rtc_get_time_tm(struct tm *time_tm) {
  const time_t time = rtc_get_time();
  localtime_r(&time, time_tm);
}

bool rtc_sanitize_struct_tm(struct tm *time_tm) {
  if (time_tm->tm_year < 100) {
    time_tm->tm_year = 100;
    return true;
  }
  if (time_tm->tm_year > 137) {
    time_tm->tm_year = 137;
    return true;
  }
  return false;
}

bool rtc_sanitize_time_t(time_t *time) {
  struct tm time_tm;
  gmtime_r(time, &time_tm);
  const bool changed = rtc_sanitize_struct_tm(&time_tm);
  *time = mktime(&time_tm);
  return changed;
}

void rtc_alarm_init(void) {
  s_alarm_start = prv_raw_ticks();
}

void rtc_alarm_set(RtcTicks num_ticks) {
  (void)num_ticks;
  s_alarm_start = prv_raw_ticks();
}

RtcTicks rtc_alarm_get_elapsed_ticks(void) {
  return prv_raw_ticks() - s_alarm_start;
}

bool rtc_alarm_is_initialized(void) {
  // A counter is available, but hardware wake/alarm routing is not yet safe.
  return false;
}

void rtc_set_timezone(TimezoneInfo *timezone) {
  const uint32_t *words = (const uint32_t *)timezone;
  _Static_assert(sizeof(TimezoneInfo) <= 5u * sizeof(uint32_t),
                 "TimezoneInfo exceeds retained RTC allocation");
  for (unsigned i = 0; i < 5; ++i) {
    t5ai_retained_write(RTC_TIMEZONE_ABBR_START + i, words[i]);
  }
}

void rtc_get_timezone(TimezoneInfo *timezone) {
  uint32_t *words = (uint32_t *)timezone;
  for (unsigned i = 0; i < 5; ++i) {
    words[i] = t5ai_retained_read(RTC_TIMEZONE_ABBR_START + i);
  }
}

uint16_t rtc_get_timezone_id(void) {
  return (uint16_t)(t5ai_retained_read(RTC_TIMEZONE_ABBR_END_TZID_DSTID) >> 16);
}

bool rtc_is_timezone_set(void) {
  return t5ai_retained_read(RTC_TIMEZONE_ABBR_START) != 0;
}

void rtc_timezone_clear(void) {
  for (unsigned i = 0; i < 5; ++i) {
    t5ai_retained_write(RTC_TIMEZONE_ABBR_START + i, 0);
  }
}

const char *time_t_to_string(char *buffer, time_t time) {
  struct tm time_tm;
  localtime_r(&time, &time_tm);
  strftime(buffer, TIME_STRING_BUFFER_SIZE, "%c", &time_tm);
  return buffer;
}

const char *rtc_get_time_string(char *buffer) {
  return time_t_to_string(buffer, rtc_get_time());
}
