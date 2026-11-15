/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/system/rtc_registers.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <pbl/drivers/i2c/t5ai.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/time.h>
#include <pbl/soc/t5ai/devices.h>
#include <pbl/util/time.h>

static time_t prv_get_retained_time(void);
#include <pbl/soc/t5ai/core.h>
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

#define PCF85063_REG_CONTROL1   0x00u
#define PCF85063_REG_SECONDS    0x04u
#define PCF85063_CONTROL1_STOP  (1u << 5)
#define PCF85063_CONTROL1_12_24 (1u << 1)
#define PCF85063_SECONDS_OS     (1u << 7)

static bool s_external_rtc_ready;
static bool s_external_rtc_present;
// First failing operation from the most recent persistence attempt (zero is healthy).
static int s_external_rtc_error;

static uint64_t prv_raw_ticks(void) {
  uint32_t low;
  uint32_t high;
  do {
    low = T5AI_AON_RTC_COUNT_LOW;
    high = T5AI_AON_RTC_COUNT_HIGH;
  } while (low != T5AI_AON_RTC_COUNT_LOW || high != T5AI_AON_RTC_COUNT_HIGH);
  return ((uint64_t)high << 32) | low;
}

static uint64_t prv_base_ticks(void) {
  return ((uint64_t)t5ai_retained_read(T5AI_RTC_BASE_TICKS_HIGH) << 32) |
         t5ai_retained_read(T5AI_RTC_BASE_TICKS_LOW);
}

static void prv_set_retained_time(time_t time) {
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  const uint64_t ticks = prv_raw_ticks();
  t5ai_retained_write_clock((uint32_t)time, ticks);
  __set_PRIMASK(mask);
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
  int result = pbl_i2c_reg_read_byte(I2C_PCF85063, PCF85063_REG_CONTROL1, control);
  if (result == 0) {
    result = pbl_i2c_burst_read(I2C_PCF85063, PCF85063_REG_SECONDS, registers,
                                PCF85063_TIME_REGISTER_COUNT);
  }
  s_external_rtc_error = result;
  return result == 0;
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
  s_external_rtc_ready = false;
  s_external_rtc_error = -1;
  // Survive reset even if a failed calendar write left plausible running contents.
  t5ai_retained_write(T5AI_RTC_EXTERNAL_UNTRUSTED, T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE);
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
  int result = pbl_i2c_reg_read_byte(I2C_PCF85063, PCF85063_REG_CONTROL1, &control);
  if (result == 0) {
    const uint8_t stopped = (control | PCF85063_CONTROL1_STOP) & ~PCF85063_CONTROL1_12_24;
    result = pbl_i2c_reg_write_byte(I2C_PCF85063, PCF85063_REG_CONTROL1, stopped);
    if (result == 0) {
      result =
          pbl_i2c_burst_write(I2C_PCF85063, PCF85063_REG_SECONDS, registers, sizeof(registers));
      // Once STOP succeeds, restart even after a partial/failed calendar write.
      const uint8_t running = control & ~(PCF85063_CONTROL1_STOP | PCF85063_CONTROL1_12_24);
      const int restart = pbl_i2c_reg_write_byte(I2C_PCF85063, PCF85063_REG_CONTROL1, running);
      if (result == 0) {
        result = restart;
      }
    }
  }

  // Cleanup success must not erase the original error or trust a partial calendar.
  if (result == 0) {
    time_t verified;
    if (!prv_external_rtc_read(&verified)) {
      result = s_external_rtc_error ? s_external_rtc_error : -1;
    } else if (verified != time && verified != time + 1) {
      result = -1;
    }
  }
  s_external_rtc_error = result;
  s_external_rtc_ready = (result == 0);
  if (s_external_rtc_ready) {
    t5ai_retained_write(T5AI_RTC_EXTERNAL_UNTRUSTED, 0);
  }
  return s_external_rtc_ready;
}

static void prv_restore_retained_time(void) {
  t5ai_retained_init();

  const uint64_t ticks = prv_raw_ticks();
  const uint64_t base_ticks = prv_base_ticks();
  uint32_t saved_time = t5ai_retained_read(CURRENT_TIME_REGISTER);
  if (saved_time < T5AI_RTC_MIN_VALID_TIME || saved_time > T5AI_RTC_MAX_VALID_TIME) {
    saved_time = T5AI_RTC_MIN_VALID_TIME;
  }
  if (base_ticks > ticks || t5ai_retained_read(CURRENT_TIME_REGISTER) != saved_time) {
    prv_set_retained_time((time_t)saved_time);
  }
}

static void prv_restore_external_time(void) {
  s_external_rtc_ready = false;
  s_external_rtc_present = false;
  uint8_t control;
  uint8_t registers[PCF85063_TIME_REGISTER_COUNT];
  if (!prv_external_rtc_read_raw(&control, registers)) {
    return;
  }
  s_external_rtc_present = true;
  time_t external_time;
  if (!t5ai_retained_read(T5AI_RTC_EXTERNAL_UNTRUSTED) &&
      prv_external_rtc_decode(control, registers, &external_time)) {
    prv_set_retained_time(external_time);
    s_external_rtc_ready = true;
    return;
  }

  /* A responding RTC with stopped/invalid contents is safe to initialize from
   * the retained fallback. Do not attempt writes when the bus/device itself
   * did not answer. Read back before trusting it for future time updates. */
  const time_t fallback_time = prv_get_retained_time();
  (void)prv_external_rtc_write(fallback_time);
}

static time_t prv_get_retained_time(void) {
  // A reader must never spin on a preempted retained-register writer.
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  const uint64_t elapsed = prv_raw_ticks() - prv_base_ticks();
  const uint32_t epoch = t5ai_retained_read(CURRENT_TIME_REGISTER);
  __set_PRIMASK(mask);
  return (time_t)epoch + (time_t)(elapsed / T5AI_AON_RTC_HZ);
}

uint64_t t5ai_rtc_get_raw_ticks(void) {
  return prv_raw_ticks();
}

static int prv_init(const struct pbl_device *dev) {
  (void)dev;
  prv_restore_retained_time();
  prv_restore_external_time();
  return 0;
}
static int prv_get_time(const struct pbl_rtc *rtc, time_t *t) {
  (void)rtc;
  *t = prv_get_retained_time();
  return 0;
}
static int prv_set_time(const struct pbl_rtc *rtc, time_t t) {
  (void)rtc;
  if (t < (time_t)T5AI_RTC_MIN_VALID_TIME || t > (time_t)T5AI_RTC_MAX_VALID_TIME)
    return -EINVAL;
  prv_set_retained_time(t);
  if (s_external_rtc_present)
    (void)prv_external_rtc_write(t);
  return s_external_rtc_present && !s_external_rtc_ready ? -EIO : 0;
}
static const struct pbl_rtc_ops s_ops = {.get_time = prv_get_time, .set_time = prv_set_time};
PBL_DEVICE_STATE_DEFINE(T5AI_RTC);
const struct pbl_rtc T5AI_RTC = {
  .dev = PBL_DEVICE_INIT_CLASS(T5AI_RTC, "pcf85063", prv_init, &T5AI_I2C0.bus.dev, nullptr,
                               &pbl_rtc_class),
  .ops = &s_ops,
};
PBL_DEVICE_REGISTER(T5AI_RTC, &T5AI_RTC.dev);
