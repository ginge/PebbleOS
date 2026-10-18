/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "system/passert.h"

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/touch/touch_sensor.h>
#include <pbl/input/input.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/regular_timer.h>
#include <pbl/services/system_task.h>

PBL_LOG_MODULE_DEFINE(driver_touch_cst92xx, CONFIG_DRIVER_TOUCH_LOG_LEVEL);

#define CST92XX_RESET_ASSERT_MS         5u
#define CST92XX_RESET_SETTLE_MS         50u
#define CST92XX_READ_RETRY_MS           80u
#define CST92XX_RESET_RETRY_MS          1000u
#define CST92XX_RESET_FAILURE_THRESHOLD 3u
#define CST92XX_FAILURE_LOG_MASK        0x3fu
#define CST92XX_STATUS_OFFSET           0u
#define CST92XX_X_HIGH_OFFSET           1u
#define CST92XX_Y_HIGH_OFFSET           2u
#define CST92XX_XY_LOW_OFFSET           3u
#define CST92XX_POINTS_OFFSET           5u
#define CST92XX_ACK_OFFSET              6u
#define CST92XX_POINTS_MASK             0x7fu
#define CST92XX_NIBBLE_MASK             0x0fu
#define CST92XX_NIBBLE_BITS             4u
#define CST92XX_INTERRUPT_PIN           42u
#define CST92XX_RESET_PIN               43u
#define CST92XX_DATA_REGISTER           0xd000u
#define CST92XX_REPORT_LENGTH           10u
#define CST92XX_REPORT_ACK              0xabu
#define CST92XX_TOUCH_STATUS            0x06u
#define CST92XX_NATIVE_SIZE             466u
#define CST92XX_POLL_MS                 20u

static bool s_enabled;
static bool s_initialized;
static bool s_callback_scheduled;
static bool s_retry_pending;
static bool s_poll_scheduled;
static uint32_t s_generation;
static uint32_t s_read_failures;
static uint8_t s_consecutive_read_failures;
static int16_t s_last_x;
static int16_t s_last_y;
static PBL_MUTEX_DEFINE(s_i2c_lock);
static TimerID s_retry_timer = TIMER_INVALID_ID;
static const struct pbl_irq_spec s_interrupt =
    PBL_IRQ_SPEC(&T5AI_GPIO_INTC, CST92XX_INTERRUPT_PIN, PBL_IRQ_TYPE_EDGE_FALLING);
static const struct pbl_gpio s_interrupt_input = PBL_GPIO(&T5AI_GPIO, CST92XX_INTERRUPT_PIN, 0);
static const struct pbl_gpio s_reset = PBL_GPIO(&T5AI_GPIO, CST92XX_RESET_PIN, 0);

static void prv_process_pending_messages(void *context);
static void prv_watchdog_cb(void *context);
static RegularTimerInfo s_watchdog_timer = {.cb = prv_watchdog_cb};

// Caller holds s_i2c_lock, including across reset delays.
static void prv_reset(bool run) {
  (void)pbl_gpio_set(&s_reset, false);
  if (run) {
    pbl_sleep_ms(CST92XX_RESET_ASSERT_MS);
    (void)pbl_gpio_set(&s_reset, true);
    pbl_sleep_ms(CST92XX_RESET_SETTLE_MS);
  }
}

static bool prv_read_report(uint8_t report[CST92XX_REPORT_LENGTH]) {
  const uint8_t address[] = {
    (uint8_t)(CST92XX_DATA_REGISTER >> 8),
    (uint8_t)CST92XX_DATA_REGISTER,
  };
  pbl_i2c_use(I2C_CST92XX);
  bool result = (pbl_i2c_write_block(I2C_CST92XX, sizeof(address), address) == 0);
  if (result) {
    result = (pbl_i2c_read_block(I2C_CST92XX, CST92XX_REPORT_LENGTH, report) == 0);
  }
  pbl_i2c_release(I2C_CST92XX);
  return result;
}

static int16_t prv_map_coordinate(uint16_t coordinate, int16_t display_size) {
  return (int16_t)(coordinate * display_size / CST92XX_NATIVE_SIZE);
}

static void prv_report_sample(bool down, int16_t x, int16_t y) {
  pbl_input_report_key(PBL_INPUT_BTN_TOUCH, down, false);
  pbl_input_report_abs(PBL_INPUT_ABS_X, x, false);
  pbl_input_report_abs(PBL_INPUT_ABS_Y, y, true);
}

static void prv_schedule(uint32_t generation) {
  pbl_irq_lock();
  if (s_enabled && generation == s_generation && !s_callback_scheduled) {
    s_callback_scheduled = true;
    s_retry_pending = !system_task_add_callback_from_isr_droppable_raised(
        prv_process_pending_messages, (void *)(uintptr_t)generation);
    if (s_retry_pending) {
      s_callback_scheduled = false;
    }
  }
  pbl_irq_unlock();
}

static void prv_interrupt_handler(void *arg) {
  prv_schedule(s_generation);
}

// NewTimer only queues work; bus transfers and reset delays belong on KernelBG.
static void prv_retry_cb(void *context) {
  const uint32_t generation = (uint32_t)(uintptr_t)context;
  pbl_irq_lock();
  if (generation == s_generation) {
    s_poll_scheduled = false;
  }
  pbl_irq_unlock();
  prv_schedule(generation);
}

static void prv_watchdog_cb(void *context) {
  pbl_irq_lock();
  const bool retry = s_enabled && !s_poll_scheduled &&
                     (s_retry_pending || pbl_gpio_get_raw(&s_interrupt_input) == 0);
  const uint32_t generation = s_generation;
  pbl_irq_unlock();
  if (retry) {
    prv_schedule(generation);
  }
}

static void prv_process_pending_messages(void *context) {
  const uint32_t generation = (uint32_t)(uintptr_t)context;
  pbl_mutex_lock(&s_i2c_lock, PBL_FOREVER);
  pbl_irq_lock();
  if (!s_enabled || generation != s_generation) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&s_i2c_lock);
    return;
  }
  s_callback_scheduled = false;
  s_poll_scheduled = false;
  pbl_irq_unlock();
  (void)new_timer_stop(s_retry_timer);

  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  bool down = false;
  uint32_t retry_ms = 0;
  if (!prv_read_report(report)) {
    ++s_read_failures;
    ++s_consecutive_read_failures;
    if (s_read_failures == 1u || (s_read_failures & CST92XX_FAILURE_LOG_MASK) == 0u) {
      PBL_LOG_ERR("CST92xx report read failed (%lu total)", (unsigned long)s_read_failures);
    }
    retry_ms = CST92XX_READ_RETRY_MS;
    if (s_consecutive_read_failures >= CST92XX_RESET_FAILURE_THRESHOLD) {
      (void)pbl_intc_disable(&s_interrupt);
      prv_reset(true);
      s_consecutive_read_failures = 0u;
      (void)pbl_intc_enable(&s_interrupt);
      retry_ms = CST92XX_RESET_RETRY_MS;
    }
  } else {
    s_consecutive_read_failures = 0u;
    const uint8_t points = report[CST92XX_POINTS_OFFSET] & CST92XX_POINTS_MASK;
    const uint8_t status = report[CST92XX_STATUS_OFFSET] & CST92XX_NIBBLE_MASK;
    if (report[CST92XX_ACK_OFFSET] == CST92XX_REPORT_ACK && points != 0u &&
        status == CST92XX_TOUCH_STATUS) {
      const uint16_t native_x = ((uint16_t)report[CST92XX_X_HIGH_OFFSET] << CST92XX_NIBBLE_BITS) |
                                (report[CST92XX_XY_LOW_OFFSET] >> CST92XX_NIBBLE_BITS);
      const uint16_t native_y = ((uint16_t)report[CST92XX_Y_HIGH_OFFSET] << CST92XX_NIBBLE_BITS) |
                                (report[CST92XX_XY_LOW_OFFSET] & CST92XX_NIBBLE_MASK);
      if (native_x < CST92XX_NATIVE_SIZE && native_y < CST92XX_NATIVE_SIZE) {
        // The panel is mounted with both touch axes mirrored.
        s_last_x = PBL_DISPLAY_WIDTH - 1 - prv_map_coordinate(native_x, PBL_DISPLAY_WIDTH);
        s_last_y = PBL_DISPLAY_HEIGHT - 1 - prv_map_coordinate(native_y, PBL_DISPLAY_HEIGHT);
        down = true;
      } else {
        PBL_LOG_WRN("CST92xx coordinate out of range: %u,%u", (unsigned)native_x,
                    (unsigned)native_y);
      }
    }
    // CST9217 needs a fallback while contact or an asserted line can strand liftoff.
    if (down || pbl_gpio_get_raw(&s_interrupt_input) == 0) {
      retry_ms = CST92XX_POLL_MS;
    }
  }

  if (retry_ms) {
    pbl_irq_lock();
    s_poll_scheduled = true;
    pbl_irq_unlock();
    PBL_ASSERTN(new_timer_start(s_retry_timer, retry_ms, prv_retry_cb, context, 0));
  }
  const int16_t x = s_last_x;
  const int16_t y = s_last_y;
  pbl_mutex_unlock(&s_i2c_lock);
  // Input listeners take service locks; never invoke them under the hardware lock.
  pbl_irq_lock();
  const bool current = s_enabled && generation == s_generation;
  pbl_irq_unlock();
  if (current) {
    prv_report_sample(down, x, y);
  }
}

void touch_sensor_init(void) {
  if (s_initialized) {
    return;
  }
  s_retry_timer = new_timer_create();
  pbl_mutex_lock(&s_i2c_lock, PBL_FOREVER);
  (void)pbl_gpio_configure(&s_reset, PBL_GPIO_OUTPUT_INACTIVE);
  prv_reset(true);
  uint8_t report[CST92XX_REPORT_LENGTH];
  if (!prv_read_report(report)) {
    PBL_LOG_ERR("CST92xx not detected at 0x5a");
  }
  prv_reset(false);
  pbl_mutex_unlock(&s_i2c_lock);
  PBL_ASSERTN(pbl_gpio_configure(&s_interrupt_input, PBL_GPIO_INPUT) == 0);
  PBL_ASSERTN(pbl_intc_request(&s_interrupt, prv_interrupt_handler, nullptr) == 0);
  s_initialized = true;
}

void touch_sensor_set_enabled(bool enabled) {
  pbl_mutex_lock(&s_i2c_lock, PBL_FOREVER);
  if (s_enabled == enabled) {
    pbl_mutex_unlock(&s_i2c_lock);
    return;
  }
  (void)pbl_intc_disable(&s_interrupt);
  pbl_irq_lock();
  s_enabled = false;
  ++s_generation;
  s_callback_scheduled = false;
  s_retry_pending = false;
  s_poll_scheduled = false;
  pbl_irq_unlock();
  (void)new_timer_stop(s_retry_timer);
  s_consecutive_read_failures = 0u;
  if (enabled) {
    prv_reset(true);
    pbl_irq_lock();
    s_enabled = true;
    pbl_irq_unlock();
    (void)pbl_intc_enable(&s_interrupt);
    if (!regular_timer_is_scheduled(&s_watchdog_timer)) {
      regular_timer_add_seconds_callback(&s_watchdog_timer);
    }
    // Read once on enable even if INT was already asserted before IRQ unmasking.
    prv_schedule(s_generation);
  } else {
    if (regular_timer_is_scheduled(&s_watchdog_timer)) {
      regular_timer_remove_callback(&s_watchdog_timer);
    }
    prv_reset(false);
  }
  pbl_mutex_unlock(&s_i2c_lock);
}
