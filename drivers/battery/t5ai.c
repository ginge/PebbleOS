/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kernel/events.h"
#include "system/status_codes.h"

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/battery.h>
#include <pbl/drivers/battery/t5ai.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/services/battery/battery_state.h>
#include <pbl/services/regular_timer.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/saradc_ipc.h>

PBL_LOG_MODULE_DEFINE(driver_battery_t5ai, LOG_LEVEL_DEBUG);
#define SAMPLE_MAX_AGE_MS  60000u
#define STARTUP_DELAY_MS   30000u
#define RETRY_DELAY_MS     5000u
#define CHARGE_DEBOUNCE_MS 400u
#define MIN_VALID_MV       2000
#define MAX_VALID_MV       5000

static bool s_initialized, s_pending, s_terminal, s_retry, s_notify, s_work_queued;
static bool s_service_requested;
static T5AISaradcDiagnostics s_last_diagnostics;
static uint16_t s_last_peak;
static bool s_charge_dirty, s_charge_known;
static uint64_t s_retry_at, s_charge_changed_at;
static struct {
  status_t status;
  int millivolts;
  uint64_t sampled_at_ms;
} s_sample = {.status = E_AGAIN};

static int prv_raw_to_millivolts(uint16_t raw) {
  const T5AIBatteryConfig *cfg = &T5AI_BATTERY_CONFIG;
  int64_t tap = ((int64_t)raw - cfg->cal_low) * 1000 / cfg->cal_span + 1000;
  return tap > 0 ? tap * cfg->divider_numerator / cfg->divider_denominator : 0;
}

static void prv_completed(status_t status, uint16_t peak, void *context) {
  (void)context;
  int millivolts = PASSED(status) ? prv_raw_to_millivolts(peak) : 0;
  if (PASSED(status) && (millivolts < MIN_VALID_MV || millivolts > MAX_VALID_MV)) {
    status = E_RANGE;
  }
  T5AISaradcDiagnostics diagnostics;
  t5ai_saradc_get_diagnostics(&diagnostics);
  pbl_irq_lock();
  s_last_diagnostics = diagnostics;
  s_last_peak = peak;
  s_sample.status = status;
  s_sample.millivolts = PASSED(status) ? millivolts : 0;
  s_sample.sampled_at_ms = pbl_uptime_ms();
  s_pending = false;
  s_terminal = status == E_AGAIN || status == E_INVALID_OPERATION;
  s_retry = FAILED(status) && !s_terminal;
  s_retry_at = s_sample.sampled_at_ms + RETRY_DELAY_MS;
  s_notify = true;
  pbl_irq_unlock();
}

static status_t prv_get_sample(int *millivolts) {
  const uint64_t now = pbl_uptime_ms();
  pbl_irq_lock();
  s_service_requested = true;
  bool fresh = PASSED(s_sample.status) && now - s_sample.sampled_at_ms < SAMPLE_MAX_AGE_MS;
  *millivolts = fresh ? s_sample.millivolts : 0;
  const bool request = !fresh && s_initialized && !s_pending && !s_terminal && now >= s_retry_at;
  status_t status = fresh ? S_SUCCESS : (FAILED(s_sample.status) ? s_sample.status : E_AGAIN);
  if (request) {
    s_pending = true;
    s_retry = false;
    s_sample.status = E_AGAIN;
    s_sample.millivolts = 0;
    status = E_AGAIN;
  }
  pbl_irq_unlock();
  if (request) {
    status_t result = t5ai_saradc_request(T5AI_BATTERY_CONFIG.channel, T5AI_BATTERY_CONFIG.samples,
                                          prv_completed, NULL);
    if (FAILED(result)) {
      prv_completed(result, 0, NULL);
      status = result;
    }
  }
  return status;
}

static void prv_service_update(void *context) {
  (void)context;
  const uint64_t now = pbl_uptime_ms();
  const bool charge = pbl_gpio_get(&T5AI_BATTERY_CONFIG.charge) > 0;
  pbl_irq_lock();
  s_work_queued = false;
  bool changed = s_charge_dirty && now - s_charge_changed_at >= CHARGE_DEBOUNCE_MS;
  if (changed) {
    s_charge_dirty = false;
    changed = charge != s_charge_known;
    s_charge_known = charge;
  }
  const bool update = s_notify || (s_service_requested && s_retry && now >= s_retry_at);
  const bool sampled = s_notify && PASSED(s_sample.status);
  const bool failed = s_notify && FAILED(s_sample.status);
  const int millivolts = s_sample.millivolts;
  const status_t status = s_sample.status;
  const T5AISaradcDiagnostics diagnostics = s_last_diagnostics;
  const uint16_t peak = s_last_peak;
  s_notify = false;
  pbl_irq_unlock();
  if (sampled) {
    PBL_LOG_DBG("Battery ADC sample: raw=%u mv=%d elapsed=%lu ms", peak, millivolts,
                (unsigned long)diagnostics.elapsed_ms);
  }
  if (failed) {
    // Logging uses synchronous CP IPC, so it must run off the mailbox owner.
    PBL_LOG_ERR("Battery ADC failed: command=%u status=%ld cp=%d elapsed=%lu ms raw=%u",
                diagnostics.failed_command, (long)status, diagnostics.cp_status,
                (unsigned long)diagnostics.elapsed_ms, peak);
  }
  if (changed) {
    // This board has CHRG only: connected remains the documented charging proxy.
    PebbleEvent event = {.type = PEBBLE_BATTERY_CONNECTION_EVENT,
                         .battery_connection = {.is_connected = charge}};
    event_put(&event);
  }
  if (update) {
    battery_state_force_update();
  }
}

static void prv_charge_interrupt(void *context) {
  (void)context;
  pbl_irq_lock();
  s_charge_dirty = true;
  s_charge_changed_at = pbl_uptime_ms();
  pbl_irq_unlock();
}

static void prv_regular_callback(void *context) {
  (void)context;
  const uint64_t now = pbl_uptime_ms();
  const bool charge = pbl_gpio_get(&T5AI_BATTERY_CONFIG.charge) > 0;
  pbl_irq_lock();
  // Reconcile a missed edge without reading the ADC or dispatching idle work.
  if (charge != s_charge_known && !s_charge_dirty) {
    s_charge_dirty = true;
    s_charge_changed_at = now;
  }
  const bool due = s_notify || (s_service_requested && s_retry && now >= s_retry_at) ||
                   (s_charge_dirty && now - s_charge_changed_at >= CHARGE_DEBOUNCE_MS);
  if (due && !s_work_queued) {
    s_work_queued = true;
    if (!system_task_add_callback_from_isr_droppable(prv_service_update, NULL)) {
      s_work_queued = false;
    }
  }
  pbl_irq_unlock();
}

void battery_init(void) {
  if (s_initialized) {
    return;
  }
  const T5AIBatteryConfig *cfg = &T5AI_BATTERY_CONFIG;
  if (cfg->cal_span == 0 || cfg->divider_denominator == 0 ||
      pbl_gpio_configure(&cfg->charge, PBL_GPIO_INPUT) != 0) {
    s_sample.status = E_INVALID_ARGUMENT;
    PBL_LOG_ERR("Invalid T5AI battery configuration");
    return;
  }
  s_charge_known = pbl_gpio_get(&cfg->charge) > 0;
  if (pbl_intc_request(&cfg->interrupt, prv_charge_interrupt, NULL) != 0 ||
      pbl_intc_enable(&cfg->interrupt) != 0) {
    PBL_LOG_WRN("Charge IRQ unavailable; reconciling GPIO state on regular timer");
  }
  s_retry_at = pbl_uptime_ms() + STARTUP_DELAY_MS;
  s_retry = true;
  s_initialized = true;
  static RegularTimerInfo timer = {.cb = prv_regular_callback};
  regular_timer_add_seconds_callback(&timer);
}

int battery_get_millivolts(void) {
  int millivolts;
  (void)prv_get_sample(&millivolts);
  return millivolts;
}

int battery_get_constants(BatteryConstants *constants) {
  if (constants == NULL) {
    return E_INVALID_ARGUMENT;
  }
  int millivolts;
  status_t status = prv_get_sample(&millivolts);
  if (FAILED(status)) {
    return status;
  }
  *constants = (BatteryConstants){.v_mv = millivolts};
  return S_SUCCESS;
}

bool battery_is_usb_connected_impl(void) {
  return pbl_gpio_get(&T5AI_BATTERY_CONFIG.charge) > 0;
}
bool battery_charge_controller_thinks_we_are_charging_impl(void) {
  return pbl_gpio_get(&T5AI_BATTERY_CONFIG.charge) > 0;
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
