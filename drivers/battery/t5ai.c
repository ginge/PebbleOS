/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/battery.h>
#include <pbl/drivers/gpio/t5ai_legacy.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

#include "system/status_codes.h"
#include <pbl/logging/logging.h>

#include <pbl/soc/t5ai/kernel.h>

#include <stdbool.h>
#include <stdint.h>

PBL_LOG_MODULE_DEFINE(driver_battery_t5ai, LOG_LEVEL_INFO);

#define T5AI_BATTERY_CHARGE_PIN 30u
#define T5AI_BATTERY_CAL_LOW_MV 1000
#define T5AI_BATTERY_CAL_SPAN_MV 1000
// Reject implausible pack readings before replacing the last valid sample.
#define T5AI_BATTERY_MIN_VALID_MV 2000
#define T5AI_BATTERY_MAX_VALID_MV 5000

#define T5AI_BATTERY_ADC_CHANNEL 15u
#define T5AI_BATTERY_ADC_SAMPLES 16u
// Meter-calibrated low/span values from the Waveshare board configuration.
#define T5AI_BATTERY_CAL_LOW 2716
#define T5AI_BATTERY_CAL_SPAN 2854
// Battery divider ratio: (2000 + 510) / 510.
#define T5AI_BATTERY_DIVIDER_NUMERATOR 2510
#define T5AI_BATTERY_DIVIDER_DENOMINATOR 510
#define T5AI_BATTERY_DEFAULT_MV 3700
#define T5AI_BATTERY_SAMPLE_INTERVAL_MS 60000u
#define T5AI_BATTERY_STARTUP_DELAY_MS 30000u
#define T5AI_BATTERY_RETRY_DELAY_MS 5000u
#define T5AI_BATTERY_TASK_STACK_WORDS 512u
#define T5AI_BATTERY_TASK_PRIORITY (PBL_PRIO_IDLE + 1u)

static const InputConfig s_charge = {.pin = T5AI_BATTERY_CHARGE_PIN};
static int s_last_millivolts = T5AI_BATTERY_DEFAULT_MV;

static int prv_raw_to_millivolts(uint16_t raw) {
  int32_t tap_mv =
      ((int32_t)raw - T5AI_BATTERY_CAL_LOW) * T5AI_BATTERY_CAL_SPAN_MV / T5AI_BATTERY_CAL_SPAN +
      T5AI_BATTERY_CAL_LOW_MV;
  if (tap_mv < 0) {
    tap_mv = 0;
  }
  return tap_mv * T5AI_BATTERY_DIVIDER_NUMERATOR / T5AI_BATTERY_DIVIDER_DENOMINATOR;
}

static void prv_battery_task(void *context) {
  (void)context;
  /* Battery initialization precedes CP service readiness. Do not mistake the
   * CP's transient SARADC startup error for a permanently poisoned session. */
  pbl_thread_sleep(PBL_MSEC(T5AI_BATTERY_STARTUP_DELAY_MS));
  for (;;) {
    uint16_t peak;
    status_t status =
        t5ai_saradc_read_peak(T5AI_BATTERY_ADC_CHANNEL, T5AI_BATTERY_ADC_SAMPLES, &peak);
    if (FAILED(status)) {
      PBL_LOG_ERR("Battery ADC read failed: %ld", (long)status);
      if (status == E_AGAIN || status == E_INVALID_OPERATION) {
        /* A timed-out MB-IPC socket is quarantined until reboot because its
         * command storage may still be owned by the CP. Avoid retry storms. */
        pbl_thread_abort(NULL);
        return;
      }
      pbl_thread_sleep(PBL_MSEC(T5AI_BATTERY_RETRY_DELAY_MS));
      continue;
    }
    const int millivolts = prv_raw_to_millivolts(peak);
    if (millivolts < T5AI_BATTERY_MIN_VALID_MV || millivolts > T5AI_BATTERY_MAX_VALID_MV) {
      PBL_LOG_ERR("Battery ADC out of range: raw=%u mv=%d", peak, millivolts);
    } else {
      __atomic_store_n(&s_last_millivolts, millivolts, __ATOMIC_RELEASE);
    }
    pbl_thread_sleep(PBL_MSEC(T5AI_BATTERY_SAMPLE_INTERVAL_MS));
  }
}

void battery_init(void) {
  gpio_input_init_pull_up_down(&s_charge, GPIO_PuPd_UP);
  if (t5ai_thread_create(prv_battery_task, "T5AI battery", T5AI_BATTERY_TASK_STACK_WORDS, NULL,
                  T5AI_BATTERY_TASK_PRIORITY, NULL, NULL) != 0) {
    PBL_LOG_ERR("Failed to create T5AI battery sampling task");
  }
}

int battery_get_millivolts(void) {
  return __atomic_load_n(&s_last_millivolts, __ATOMIC_ACQUIRE);
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
