/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "kernel/events.h"
#include "kernel/pebble_tasks.h"
#include "pbl/services/touch/touch.h"
#include "system/passert.h"

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/gpio/t5ai.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/touch/touch_sensor.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/kernel.h>
#include <pbl/util/math.h>

PBL_LOG_MODULE_DEFINE(driver_touch_cst92xx, CONFIG_DRIVER_TOUCH_LOG_LEVEL);

#define CST92XX_RESET_ASSERT_MS         5u
#define CST92XX_RESET_SETTLE_MS         50u
#define CST92XX_READ_RETRY_MS           80u
#define CST92XX_RESET_RETRY_MS          1000u
#define CST92XX_RESET_FAILURE_THRESHOLD 3u
#define CST92XX_FAILURE_LOG_MASK        0x3fu // Report every 64 failures after the first.
#define CST92XX_DELIVERY_QUEUE_LIMIT    2u
// CST9217 report packs each 12-bit coordinate into a high byte and low nibble.
#define CST92XX_STATUS_OFFSET 0u
#define CST92XX_X_HIGH_OFFSET 1u
#define CST92XX_Y_HIGH_OFFSET 2u
#define CST92XX_XY_LOW_OFFSET 3u
#define CST92XX_POINTS_OFFSET 5u
#define CST92XX_ACK_OFFSET    6u
#define CST92XX_POINTS_MASK   0x7fu
#define CST92XX_NIBBLE_MASK   0x0fu
#define CST92XX_NIBBLE_BITS   4u

#define CST92XX_INTERRUPT_PIN      42u
#define CST92XX_RESET_PIN          43u
#define CST92XX_DATA_REGISTER      0xd000u
#define CST92XX_REPORT_LENGTH      10u
#define CST92XX_REPORT_ACK         0xabu
#define CST92XX_TOUCH_STATUS       0x06u
#define CST92XX_NATIVE_SIZE        466u
#define CST92XX_POLL_MS            20u
#define CST92XX_MOTION_INTERVAL_MS 33u
#define CST92XX_TASK_STACK_WORDS   768u
// Stay above KernelMain so polling remains responsive, but leave NewTimer at the sole maximum
// priority. NewTimer owns watchdog and mailbox work that must be able to preempt I2C recovery.
#define CST92XX_TASK_PRIORITY (CONFIG_KERNEL_NUM_PRIORITIES - 2)

static volatile bool s_enabled;
static bool s_finger_down;
static bool s_contact_active;
static bool s_release_pending;
static int16_t s_last_x;
static int16_t s_last_y;
static pbl_tick_t s_last_motion_ticks;
static uint32_t s_read_failures;
static uint8_t s_consecutive_read_failures;
static struct pbl_sem *s_touch_wake;
static const struct pbl_irq_spec s_interrupt =
    PBL_IRQ_SPEC(&T5AI_GPIO_INTC, CST92XX_INTERRUPT_PIN, PBL_IRQ_TYPE_EDGE_FALLING);
static const struct pbl_gpio s_interrupt_input = PBL_GPIO(&T5AI_GPIO, CST92XX_INTERRUPT_PIN, 0);
static const struct pbl_gpio s_reset = PBL_GPIO(&T5AI_GPIO, CST92XX_RESET_PIN, 0);

static void prv_reset(bool run) {
  if (!run) {
    (void)pbl_gpio_set(&s_reset, false);
    return;
  }
  (void)pbl_gpio_set(&s_reset, false);
  pbl_sleep_ms(CST92XX_RESET_ASSERT_MS);
  (void)pbl_gpio_set(&s_reset, true);
  pbl_sleep_ms(CST92XX_RESET_SETTLE_MS);
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

// A frame-rate limit alone still outruns a slow app. Keep motion in the driver
// while either delivery stage is backed up, leaving queue space for liftoff and
// unrelated services. Inspect handles with scheduling stopped so app teardown
// cannot delete its queue between the lookup and the occupancy check.
static bool prv_delivery_ready(void) {
  pbl_irq_lock();
  struct pbl_msgq *app = pebble_task_get_to_queue(PebbleTask_App);
  struct pbl_msgq *worker = pebble_task_get_to_queue(PebbleTask_Worker);
  struct pbl_msgq *kernel = event_get_to_kernel_queue(PebbleTask_KernelBackground);
  const bool ready = (!app || pbl_msgq_num_used(app) < CST92XX_DELIVERY_QUEUE_LIMIT) &&
                     (!worker || pbl_msgq_num_used(worker) < CST92XX_DELIVERY_QUEUE_LIMIT) &&
                     (!kernel || pbl_msgq_num_used(kernel) < CST92XX_DELIVERY_QUEUE_LIMIT);
  pbl_irq_unlock();
  return ready;
}

static bool prv_poll(void) {
  if (!s_enabled) {
    return true;
  }
  if (s_release_pending && prv_delivery_ready()) {
    touch_handle_update(TouchState_FingerDown, s_last_x, s_last_y);
    touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
    s_finger_down = false;
    s_release_pending = false;
  }
  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  if (!prv_read_report(report)) {
    ++s_read_failures;
    ++s_consecutive_read_failures;
    if (s_read_failures == 1u || (s_read_failures & CST92XX_FAILURE_LOG_MASK) == 0u) {
      PBL_LOG_ERR("CST92xx report read failed (%lu total)", (unsigned long)s_read_failures);
    }
    if (s_consecutive_read_failures >= CST92XX_RESET_FAILURE_THRESHOLD) {
      prv_reset(true);
      s_consecutive_read_failures = 0u;
    }
    return false;
  }
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
      // The panel is mounted with both touch axes mirrored. This matches the exact Waveshare board
      // configuration and was confirmed by physical path telemetry (finger-up read as increasing
      // Y).
      const int16_t x = PBL_DISPLAY_WIDTH - 1 - prv_map_coordinate(native_x, PBL_DISPLAY_WIDTH);
      const int16_t y = PBL_DISPLAY_HEIGHT - 1 - prv_map_coordinate(native_y, PBL_DISPLAY_HEIGHT);
      s_contact_active = true;
      if (s_release_pending) {
        return true;
      }
      s_last_x = x;
      s_last_y = y;
      const pbl_tick_t now = pbl_uptime_ticks();
      // Sample every report, but bound event delivery to the UI frame budget.
      // Touchdown is immediate; liftoff flushes the latest sampled position.
      if (prv_delivery_ready() &&
          (!s_finger_down ||
           now - s_last_motion_ticks >= pbl_ms_to_ticks(CST92XX_MOTION_INTERVAL_MS))) {
        touch_handle_update(TouchState_FingerDown, s_last_x, s_last_y);
        s_last_motion_ticks = now;
        s_finger_down = true;
      }
      return true;
    }
    PBL_LOG_WRN("CST92xx coordinate out of range: %u,%u", (unsigned)native_x, (unsigned)native_y);
  }

  s_contact_active = false;
  if (s_finger_down && !prv_delivery_ready()) {
    s_release_pending = true;
    return true;
  }
  if (s_finger_down) {
    touch_handle_update(TouchState_FingerDown, s_last_x, s_last_y);
    touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
    s_finger_down = false;
  }
  return true;
}

static void prv_interrupt_handler(void *arg) {
  pbl_sem_give(s_touch_wake);
}

static void prv_touch_task(void *context) {
  (void)context;
  while (true) {
    // Idle with no periodic wakeups. GPIO42's falling edge is the primary
    // source of touch work; enabling also sends one notification so a level
    // already asserted during reset cannot be missed.
    (void)pbl_sem_take(s_touch_wake, PBL_TICKS(PBL_TICK_FOREVER));
    while (s_enabled) {
      const bool read_succeeded = prv_poll();
      if (!s_enabled) {
        break;
      }

      if (!read_succeeded) {
        // After reset, retry slowly even if the controller never produces a new edge.
        const uint32_t retry_ms =
            s_consecutive_read_failures == 0u ? CST92XX_RESET_RETRY_MS : CST92XX_READ_RETRY_MS;
        (void)pbl_sem_take(s_touch_wake, PBL_TICKS(pbl_ms_to_ticks(retry_ms)));
        continue;
      }

      if (!s_finger_down && !s_contact_active && (pbl_gpio_get_raw(&s_interrupt_input) > 0)) {
        break;
      }

      // CST9217 normally interrupts for each fresh report. While a contact is
      // active, use 20 ms only as a lost-edge fallback so motion and finger-up
      // cannot become stranded behind an asserted interrupt line.
      (void)pbl_sem_take(s_touch_wake, PBL_TICKS(pbl_ms_to_ticks(CST92XX_POLL_MS)));
    }
  }
}

void touch_sensor_init(void) {
  (void)pbl_gpio_configure(&s_reset, PBL_GPIO_OUTPUT_INACTIVE);
  prv_reset(true);
  uint8_t report[CST92XX_REPORT_LENGTH];
  if (!prv_read_report(report)) {
    PBL_LOG_ERR("CST92xx not detected at 0x5a");
  }
  prv_reset(false);
  s_touch_wake = t5ai_sem_create(1, 0);
  PBL_ASSERTN(s_touch_wake != NULL);
  const int created = t5ai_thread_create(prv_touch_task, "CST92xx", CST92XX_TASK_STACK_WORDS, NULL,
                                         CST92XX_TASK_PRIORITY, NULL, NULL);
  PBL_ASSERTN(created == 0);
  PBL_ASSERTN(pbl_gpio_configure(&s_interrupt_input, PBL_GPIO_INPUT) == 0);
  PBL_ASSERTN(pbl_intc_request(&s_interrupt, prv_interrupt_handler, nullptr) == 0);
}

void touch_sensor_set_enabled(bool enabled) {
  if (s_enabled == enabled) {
    return;
  }
  if (enabled) {
    prv_reset(true);
    s_enabled = true;
    (void)pbl_intc_enable(&s_interrupt);
    (void)pbl_sem_give(s_touch_wake);
  } else {
    s_enabled = false;
    (void)pbl_intc_disable(&s_interrupt);
    (void)pbl_sem_give(s_touch_wake);
    if (s_finger_down) {
      touch_handle_update(TouchState_FingerDown, s_last_x, s_last_y);
      touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
      s_finger_down = false;
    }
    s_contact_active = false;
    s_release_pending = false;
    prv_reset(false);
  }
}
