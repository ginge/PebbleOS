/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "system/rtc_registers.h"

#include <stddef.h>
#include <string.h>

#include <pbl/drivers/i2c.h>
#include <pbl/drivers/rtc.h>
#include <pbl/soc/t5ai/retained.h>
#include <pbl/soc/t5ai/rtc.h>

// External RTC stores years 00..37 relative to 2000; struct pbl_tm counts from 1900.
#define PCF85063_TM_YEAR_BASE        100
#define PCF85063_MAX_YEAR            37
#define PCF85063_TM_YEAR_MAX         (PCF85063_TM_YEAR_BASE + PCF85063_MAX_YEAR)
#define PCF85063_TIME_REGISTER_COUNT 7u
#define PCF85063_SECONDS_OFFSET      0u
#define PCF85063_MINUTES_OFFSET      1u
#define PCF85063_HOURS_OFFSET        2u
#define PCF85063_DAYS_OFFSET         3u
#define PCF85063_WEEKDAYS_OFFSET     4u
#define PCF85063_MONTHS_OFFSET       5u
#define PCF85063_YEARS_OFFSET        6u
#define PCF85063_SECONDS_MASK        0x7fu
#define PCF85063_MINUTES_MASK        0x7fu
#define PCF85063_HOURS_MASK          0x3fu
#define PCF85063_DAYS_MASK           0x3fu
#define PCF85063_WEEKDAYS_MASK       0x07u
#define PCF85063_MONTHS_MASK         0x1fu
#define RTC_BCD_DIGIT_MASK           0x0fu
#define RTC_BCD_TENS_SHIFT           4u
#define T5AI_RTC_TIMEZONE_WORDS      5u

// Match services_normal's activity threshold so an empty/invalid external RTC
// initializes Activity during boot, before T5AI starts advertising. The phone's
// later SetTime then remains a lightweight clock-change event.
#define T5AI_RTC_MIN_VALID_TIME 1577836800u
#define T5AI_RTC_MAX_VALID_TIME 2145916799u

_Static_assert(T5AI_AON_RTC_HZ % RTC_TICKS_HZ == 0u,
               "AON RTC must divide exactly into Pebble ticks");

#define PCF85063_REG_CONTROL1   0x00u
#define PCF85063_REG_SECONDS    0x04u
#define PCF85063_CONTROL1_STOP  (1u << 5)
#define PCF85063_CONTROL1_12_24 (1u << 1)
#define PCF85063_SECONDS_OS     (1u << 7)

static RtcTicks s_alarm_start;
static bool s_external_rtc_ready;

static RtcTicks prv_raw_ticks(void) {
  uint32_t low;
  uint32_t high;
  do {
    low = T5AI_AON_RTC_COUNT_LOW;
    high = T5AI_AON_RTC_COUNT_HIGH;
  } while (low != T5AI_AON_RTC_COUNT_LOW || high != T5AI_AON_RTC_COUNT_HIGH);
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

static bool prv_decode_bcd(uint8_t raw, uint8_t minimum, uint8_t maximum, uint8_t *value) {
  if ((raw & RTC_BCD_DIGIT_MASK) > 9u || (raw >> RTC_BCD_TENS_SHIFT) > 9u) {
    return false;
  }
  *value = (raw >> RTC_BCD_TENS_SHIFT) * 10u + (raw & RTC_BCD_DIGIT_MASK);
  return *value >= minimum && *value <= maximum;
}

static uint8_t prv_encode_bcd(unsigned value) {
  return (uint8_t)(((value / 10u) << RTC_BCD_TENS_SHIFT) | (value % 10u));
}

static bool prv_external_rtc_read_raw(uint8_t *control,
                                      uint8_t registers[PCF85063_TIME_REGISTER_COUNT]) {
  pbl_i2c_use(I2C_PCF85063);
  const bool read_control =
      (pbl_i2c_read_register(I2C_PCF85063, PCF85063_REG_CONTROL1, control) == 0);
  const bool read_time =
      read_control && (pbl_i2c_read_register_block(I2C_PCF85063, PCF85063_REG_SECONDS,
                                                   PCF85063_TIME_REGISTER_COUNT, registers) == 0);
  pbl_i2c_release(I2C_PCF85063);
  return read_time;
}

static bool prv_external_rtc_decode(uint8_t control,
                                    const uint8_t registers[PCF85063_TIME_REGISTER_COUNT],
                                    time_t *time) {
  if ((control & (PCF85063_CONTROL1_STOP | PCF85063_CONTROL1_12_24)) ||
      (registers[PCF85063_SECONDS_OFFSET] & PCF85063_SECONDS_OS)) {
    return false;
  }

  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t month;
  uint8_t year;
  const uint8_t weekday = registers[PCF85063_WEEKDAYS_OFFSET] & PCF85063_WEEKDAYS_MASK;
  if (!prv_decode_bcd(registers[PCF85063_SECONDS_OFFSET] & PCF85063_SECONDS_MASK, 0, 59, &second) ||
      !prv_decode_bcd(registers[PCF85063_MINUTES_OFFSET] & PCF85063_MINUTES_MASK, 0, 59, &minute) ||
      !prv_decode_bcd(registers[PCF85063_HOURS_OFFSET] & PCF85063_HOURS_MASK, 0, 23, &hour) ||
      !prv_decode_bcd(registers[PCF85063_DAYS_OFFSET] & PCF85063_DAYS_MASK, 1, 31, &day) ||
      weekday > 6u ||
      !prv_decode_bcd(registers[PCF85063_MONTHS_OFFSET] & PCF85063_MONTHS_MASK, 1, 12, &month) ||
      !prv_decode_bcd(registers[PCF85063_YEARS_OFFSET], 0, PCF85063_MAX_YEAR, &year)) {
    return false;
  }

  struct pbl_tm decoded = {
    .tm_sec = second,
    .tm_min = minute,
    .tm_hour = hour,
    .tm_mday = day,
    .tm_mon = month - 1,
    .tm_year = year + PCF85063_TM_YEAR_BASE,
    .tm_isdst = 0,
  };
  const time_t result = pbl_mktime(&decoded);
  if (decoded.tm_sec != second || decoded.tm_min != minute || decoded.tm_hour != hour ||
      decoded.tm_mday != day || decoded.tm_mon != month - 1 ||
      decoded.tm_year != year + PCF85063_TM_YEAR_BASE || decoded.tm_wday != weekday) {
    return false;
  }
  *time = result;
  return true;
}

static bool prv_external_rtc_read(time_t *time) {
  uint8_t control;
  uint8_t registers[PCF85063_TIME_REGISTER_COUNT];
  return prv_external_rtc_read_raw(&control, registers) &&
         prv_external_rtc_decode(control, registers, time);
}

static bool prv_external_rtc_write(time_t time) {
  struct pbl_tm encoded;
  pbl_gmtime_r(&time, &encoded);
  if (encoded.tm_year < PCF85063_TM_YEAR_BASE || encoded.tm_year > PCF85063_TM_YEAR_MAX) {
    return false;
  }

  const uint8_t registers[PCF85063_TIME_REGISTER_COUNT] = {
    prv_encode_bcd(encoded.tm_sec),
    prv_encode_bcd(encoded.tm_min),
    prv_encode_bcd(encoded.tm_hour),
    prv_encode_bcd(encoded.tm_mday),
    (uint8_t)encoded.tm_wday,
    prv_encode_bcd(encoded.tm_mon + 1),
    prv_encode_bcd(encoded.tm_year - PCF85063_TM_YEAR_BASE),
  };

  uint8_t control;
  pbl_i2c_use(I2C_PCF85063);
  bool result = (pbl_i2c_read_register(I2C_PCF85063, PCF85063_REG_CONTROL1, &control) == 0);
  if (result) {
    const uint8_t stopped = (control | PCF85063_CONTROL1_STOP) & ~PCF85063_CONTROL1_12_24;
    result = (pbl_i2c_write_register(I2C_PCF85063, PCF85063_REG_CONTROL1, stopped) == 0);
    result = result && (pbl_i2c_write_register_block(I2C_PCF85063, PCF85063_REG_SECONDS,
                                                     sizeof(registers), registers) == 0);
    if (result) {
      const uint8_t running = control & ~(PCF85063_CONTROL1_STOP | PCF85063_CONTROL1_12_24);
      result = (pbl_i2c_write_register(I2C_PCF85063, PCF85063_REG_CONTROL1, running) == 0);
    }
  }
  pbl_i2c_release(I2C_PCF85063);
  return result;
}

void rtc_init(void) {
  t5ai_retained_init();

  const RtcTicks ticks = prv_raw_ticks();
  const RtcTicks base_ticks = prv_base_ticks();
  uint32_t saved_time = t5ai_retained_read(CURRENT_TIME_REGISTER);
  if (saved_time < T5AI_RTC_MIN_VALID_TIME || saved_time > T5AI_RTC_MAX_VALID_TIME) {
    saved_time = T5AI_RTC_MIN_VALID_TIME;
  }
  if (base_ticks > ticks || t5ai_retained_read(CURRENT_TIME_REGISTER) != saved_time) {
    prv_set_retained_time((time_t)saved_time);
  }
}

void rtc_calibrate_frequency(uint32_t frequency) {
  (void)frequency;
}

void rtc_init_timers(void) {
  s_external_rtc_ready = false;
  uint8_t control;
  uint8_t registers[PCF85063_TIME_REGISTER_COUNT];
  if (!prv_external_rtc_read_raw(&control, registers)) {
    return;
  }
  time_t external_time;
  if (prv_external_rtc_decode(control, registers, &external_time)) {
    prv_set_retained_time(external_time);
    s_external_rtc_ready = true;
    return;
  }

  /* A responding RTC with stopped/invalid contents is safe to initialize from
   * the retained fallback. Do not attempt writes when the bus/device itself
   * did not answer. Read back before trusting it for future time updates. */
  const time_t fallback_time = rtc_get_time();
  if (prv_external_rtc_write(fallback_time) && prv_external_rtc_read(&external_time)) {
    prv_set_retained_time(external_time);
    s_external_rtc_ready = true;
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
  *out_seconds =
      (time_t)t5ai_retained_read(CURRENT_TIME_REGISTER) + (time_t)(elapsed / T5AI_AON_RTC_HZ);
  *out_ms = (uint16_t)(((elapsed % T5AI_AON_RTC_HZ) * 1000u) / T5AI_AON_RTC_HZ);
}

time_t rtc_get_time(void) {
  time_t seconds;
  uint16_t milliseconds;
  rtc_get_time_ms(&seconds, &milliseconds);
  return seconds;
}

RtcTicks rtc_get_ticks(void) {
  return prv_raw_ticks() / (T5AI_AON_RTC_HZ / RTC_TICKS_HZ);
}

uint64_t t5ai_rtc_get_raw_ticks(void) {
  return prv_raw_ticks();
}

void rtc_set_time_tm(struct pbl_tm *time_tm) {
  rtc_set_time(pbl_mktime(time_tm));
}

void rtc_get_time_tm(struct pbl_tm *time_tm) {
  const time_t time = rtc_get_time();
  pbl_localtime_r(&time, time_tm);
}

bool rtc_sanitize_struct_tm(struct pbl_tm *time_tm) {
  if (time_tm->tm_year < PCF85063_TM_YEAR_BASE) {
    time_tm->tm_year = PCF85063_TM_YEAR_BASE;
    return true;
  }
  if (time_tm->tm_year > PCF85063_TM_YEAR_MAX) {
    time_tm->tm_year = PCF85063_TM_YEAR_MAX;
    return true;
  }
  return false;
}

bool rtc_sanitize_time_t(time_t *time) {
  struct pbl_tm time_tm;
  pbl_gmtime_r(time, &time_tm);
  const bool changed = rtc_sanitize_struct_tm(&time_tm);
  *time = pbl_mktime(&time_tm);
  return changed;
}

void rtc_alarm_init(void) {
  s_alarm_start = rtc_get_ticks();
}

void rtc_alarm_set(RtcTicks num_ticks) {
  (void)num_ticks;
  s_alarm_start = rtc_get_ticks();
}

RtcTicks rtc_alarm_get_elapsed_ticks(void) {
  return rtc_get_ticks() - s_alarm_start;
}

bool rtc_alarm_is_initialized(void) {
  // A counter is available, but hardware wake/alarm routing is not yet safe.
  return false;
}

void rtc_set_timezone(TimezoneInfo *timezone) {
  const uint32_t *words = (const uint32_t *)timezone;
  _Static_assert(sizeof(TimezoneInfo) <= T5AI_RTC_TIMEZONE_WORDS * sizeof(uint32_t),
                 "TimezoneInfo exceeds retained RTC allocation");
  for (unsigned i = 0; i < T5AI_RTC_TIMEZONE_WORDS; ++i) {
    t5ai_retained_write(RTC_TIMEZONE_ABBR_START + i, words[i]);
  }
}

void rtc_get_timezone(TimezoneInfo *timezone) {
  uint32_t *words = (uint32_t *)timezone;
  for (unsigned i = 0; i < T5AI_RTC_TIMEZONE_WORDS; ++i) {
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
  for (unsigned i = 0; i < T5AI_RTC_TIMEZONE_WORDS; ++i) {
    t5ai_retained_write(RTC_TIMEZONE_ABBR_START + i, 0);
  }
}

const char *time_t_to_string(char *buffer, time_t time) {
  struct pbl_tm time_tm;
  pbl_localtime_r(&time, &time_tm);
  pbl_strftime_r(buffer, TIME_STRING_BUFFER_SIZE, "%c", &time_tm);
  return buffer;
}

const char *rtc_get_time_string(char *buffer) {
  return time_t_to_string(buffer, rtc_get_time());
}
