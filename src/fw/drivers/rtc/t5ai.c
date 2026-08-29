/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rtc.h>
#include <pbl/soc/t5ai/retained.h>

#include "system/rtc_registers.h"

#include <stddef.h>
#include <string.h>

#define T5AI_AON_RTC_BASE 0x44000200u
#define T5AI_AON_RTC_COUNT_LOW (*(volatile uint32_t *)(T5AI_AON_RTC_BASE + 0x0cu))
#define T5AI_AON_RTC_COUNT_HIGH (*(volatile uint32_t *)(T5AI_AON_RTC_BASE + 0x28u))

static RtcTicks s_alarm_start;

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

void rtc_init(void) {
  t5ai_retained_init();
}

void rtc_calibrate_frequency(uint32_t frequency) {
  (void)frequency;
}

void rtc_init_timers(void) {}

void rtc_set_time(time_t time) {
  const RtcTicks ticks = prv_raw_ticks();
  t5ai_retained_write(CURRENT_TIME_REGISTER, (uint32_t)time);
  t5ai_retained_write(T5AI_RTC_BASE_TICKS_LOW, (uint32_t)ticks);
  t5ai_retained_write(T5AI_RTC_BASE_TICKS_HIGH, (uint32_t)(ticks >> 32));
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
