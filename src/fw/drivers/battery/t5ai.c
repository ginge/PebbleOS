/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/battery.h>
#include <pbl/drivers/gpio.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

#include "system/status_codes.h"
#include <pbl/logging/logging.h>

#include <stdbool.h>
#include <stdint.h>

PBL_LOG_MODULE_DEFINE(driver_battery_t5ai, LOG_LEVEL_INFO);

#define T5AI_BATTERY_ADC_CHANNEL 15u
#define T5AI_BATTERY_ADC_SAMPLES 16u
#define T5AI_BATTERY_CAL_LOW 2716
#define T5AI_BATTERY_CAL_SPAN 2854
#define T5AI_BATTERY_DIVIDER_NUMERATOR 2510
#define T5AI_BATTERY_DIVIDER_DENOMINATOR 510
#define T5AI_BATTERY_DEFAULT_MV 3700

static const InputConfig s_charge = {.pin = 30u};
static int s_last_millivolts = T5AI_BATTERY_DEFAULT_MV;

static int prv_raw_to_millivolts(uint16_t raw) {
  int32_t tap_mv = ((int32_t)raw - T5AI_BATTERY_CAL_LOW) * 1000 / T5AI_BATTERY_CAL_SPAN + 1000;
  if (tap_mv < 0) {
    tap_mv = 0;
  }
  return tap_mv * T5AI_BATTERY_DIVIDER_NUMERATOR / T5AI_BATTERY_DIVIDER_DENOMINATOR;
}

void battery_init(void) {
  gpio_input_init(&s_charge);
}

int battery_get_millivolts(void) {
  uint16_t peak;
  status_t status =
      t5ai_saradc_read_peak(T5AI_BATTERY_ADC_CHANNEL, T5AI_BATTERY_ADC_SAMPLES, &peak);
  if (FAILED(status)) {
    PBL_LOG_ERR("Battery ADC read failed: %ld", (long)status);
    return s_last_millivolts;
  }
  const int millivolts = prv_raw_to_millivolts(peak);
  if (millivolts < 2000 || millivolts > 5000) {
    PBL_LOG_ERR("Battery ADC out of range: raw=%u mv=%d", peak, millivolts);
    return s_last_millivolts;
  }
  s_last_millivolts = millivolts;
  return millivolts;
}

int battery_get_constants(BatteryConstants *constants) {
  if (constants == NULL) {
    return E_INVALID_ARGUMENT;
  }
  *constants = (BatteryConstants){
      .v_mv = battery_get_millivolts(),
      .i_ua = 0,
      .t_mc = 0,
  };
  return S_SUCCESS;
}

bool battery_is_usb_connected_impl(void) {
  return !gpio_input_read(&s_charge);
}

bool battery_charge_controller_thinks_we_are_charging_impl(void) {
  return !gpio_input_read(&s_charge);
}

void battery_set_charge_enable(bool charging_enabled) {
  (void)charging_enabled;
}

void battery_set_fast_charge(bool fast_charge_enabled) {
  (void)fast_charge_enabled;
}

int battery_charge_status_get(BatteryChargeStatus *status) {
  if (status == NULL) {
    return E_INVALID_ARGUMENT;
  }
  *status = battery_charge_controller_thinks_we_are_charging_impl() ? BatteryChargeStatusCC
                                                                    : BatteryChargeStatusUnknown;
  return S_SUCCESS;
}
