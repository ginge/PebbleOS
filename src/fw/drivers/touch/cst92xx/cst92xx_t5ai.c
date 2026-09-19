/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/touch/touch_sensor.h>

#include "board/board.h"
#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/os/tick.h>
#include "kernel/util/sleep.h"
#include "pbl/services/touch/touch.h"
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

PBL_LOG_MODULE_DEFINE(driver_touch_cst92xx, CONFIG_DRIVER_TOUCH_LOG_LEVEL);

#define CST92XX_INTERRUPT_PIN 42u
#define CST92XX_RESET_PIN 43u
#define CST92XX_DATA_REGISTER 0xd000u
#define CST92XX_REPORT_LENGTH 10u
#define CST92XX_REPORT_ACK 0xabu
#define CST92XX_TOUCH_STATUS 0x06u
#define CST92XX_NATIVE_SIZE 466u
#define CST92XX_POLL_MS 20u
#define CST92XX_TASK_STACK_WORDS 768u
// Stay above KernelMain so polling remains responsive, but leave NewTimer at the sole maximum
// priority. NewTimer owns watchdog and mailbox work that must be able to preempt I2C recovery.
#define CST92XX_TASK_PRIORITY (configMAX_PRIORITIES - 2)

static volatile bool s_enabled;
static bool s_finger_down;
static int16_t s_last_x;
static int16_t s_last_y;
static uint32_t s_read_failures;
static uint8_t s_consecutive_read_failures;
static SemaphoreHandle_t s_touch_wake;
static const ExtiConfig s_interrupt = {
    .pin = CST92XX_INTERRUPT_PIN,
    .pull = GPIO_PuPd_NOPULL,
};
static const InputConfig s_interrupt_input = {
    .pin = CST92XX_INTERRUPT_PIN,
};
static const OutputConfig s_reset = {
    .pin = CST92XX_RESET_PIN,
    .active_high = true,
};

static void prv_reset(bool run) {
  if (!run) {
    gpio_output_set(&s_reset, false);
    return;
  }
  gpio_output_set(&s_reset, false);
  psleep(5);
  gpio_output_set(&s_reset, true);
  psleep(50);
}

static bool prv_read_report(uint8_t report[CST92XX_REPORT_LENGTH]) {
  const uint8_t address[] = {
      (uint8_t)(CST92XX_DATA_REGISTER >> 8),
      (uint8_t)CST92XX_DATA_REGISTER,
  };
  i2c_use(I2C_CST92XX);
  bool result = i2c_write_block(I2C_CST92XX, sizeof(address), address);
  if (result) {
    result = i2c_read_block(I2C_CST92XX, CST92XX_REPORT_LENGTH, report);
  }
  i2c_release(I2C_CST92XX);
  return result;
}

static int16_t prv_map_coordinate(uint16_t coordinate, int16_t display_size) {
  return (int16_t)(coordinate * display_size / CST92XX_NATIVE_SIZE);
}

static bool prv_poll(void) {
  if (!s_enabled) {
    return true;
  }
  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  if (!prv_read_report(report)) {
    ++s_read_failures;
    ++s_consecutive_read_failures;
    if (s_read_failures == 1u || (s_read_failures & 0x3fu) == 0u) {
      PBL_LOG_ERR("CST92xx report read failed (%lu total)", (unsigned long)s_read_failures);
    }
    if (s_consecutive_read_failures >= 3u) {
      prv_reset(true);
      s_consecutive_read_failures = 0u;
    }
    return false;
  }
  s_consecutive_read_failures = 0u;
  const uint8_t points = report[5] & 0x7fu;
  const uint8_t status = report[0] & 0x0fu;
  if (report[6] == CST92XX_REPORT_ACK && points != 0u && status == CST92XX_TOUCH_STATUS) {
    const uint16_t native_x = ((uint16_t)report[1] << 4) | (report[3] >> 4);
    const uint16_t native_y = ((uint16_t)report[2] << 4) | (report[3] & 0x0fu);
    if (native_x < CST92XX_NATIVE_SIZE && native_y < CST92XX_NATIVE_SIZE) {
      // The panel is mounted with both touch axes mirrored. This matches the exact Waveshare board
      // configuration and was confirmed by physical path telemetry (finger-up read as increasing Y).
      const int16_t x = PBL_DISPLAY_WIDTH - 1 -
                        prv_map_coordinate(native_x, PBL_DISPLAY_WIDTH);
      const int16_t y = PBL_DISPLAY_HEIGHT - 1 -
                        prv_map_coordinate(native_y, PBL_DISPLAY_HEIGHT);
      s_last_x = x;
      s_last_y = y;
      touch_handle_update(TouchState_FingerDown, s_last_x, s_last_y);
      s_finger_down = true;
      return true;
    }
    PBL_LOG_WRN("CST92xx coordinate out of range: %u,%u", (unsigned)native_x, (unsigned)native_y);
  }

  if (s_finger_down) {
    touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
    s_finger_down = false;
  }
  return true;
}

static void prv_interrupt_handler(bool *should_context_switch) {
  BaseType_t task_woken = pdFALSE;
  (void)xSemaphoreGiveFromISR(s_touch_wake, &task_woken);
  if (task_woken == pdTRUE) {
    *should_context_switch = true;
  }
}

static void prv_touch_task(void *context) {
  (void)context;
  while (true) {
    // Idle with no periodic wakeups. GPIO42's falling edge is the primary
    // source of touch work; enabling also sends one notification so a level
    // already asserted during reset cannot be missed.
    (void)xSemaphoreTake(s_touch_wake, portMAX_DELAY);
    while (s_enabled) {
      const bool read_succeeded = prv_poll();
      if (!s_enabled) {
        break;
      }

      if (!read_succeeded) {
        if (s_consecutive_read_failures == 0u) {
          // The third failure reset the controller. Wait for its next IRQ
          // rather than turning a disconnected panel into a polling loop.
          break;
        }
        (void)xSemaphoreTake(s_touch_wake, milliseconds_to_ticks(80u));
        continue;
      }

      if (!s_finger_down && gpio_input_read(&s_interrupt_input)) {
        break;
      }

      // CST9217 normally interrupts for each fresh report. While a contact is
      // active, use 20 ms only as a lost-edge fallback so motion and finger-up
      // cannot become stranded behind an asserted interrupt line.
      (void)xSemaphoreTake(s_touch_wake, milliseconds_to_ticks(CST92XX_POLL_MS));
    }
  }
}

void touch_sensor_init(void) {
  gpio_output_init(&s_reset, GPIO_OType_PP);
  prv_reset(true);
  uint8_t report[CST92XX_REPORT_LENGTH];
  if (!prv_read_report(report)) {
    PBL_LOG_ERR("CST92xx not detected at 0x5a");
  }
  prv_reset(false);
  s_touch_wake = xSemaphoreCreateBinary();
  PBL_ASSERTN(s_touch_wake != NULL);
  const BaseType_t created =
      xTaskCreate(prv_touch_task, "CST92xx", CST92XX_TASK_STACK_WORDS, NULL,
                  CST92XX_TASK_PRIORITY, NULL);
  PBL_ASSERTN(created == pdPASS);
  exti_configure_pin(s_interrupt, ExtiTrigger_Falling, prv_interrupt_handler);
}

void touch_sensor_set_enabled(bool enabled) {
  if (s_enabled == enabled) {
    return;
  }
  if (enabled) {
    prv_reset(true);
    s_enabled = true;
    exti_enable(s_interrupt);
    (void)xSemaphoreGive(s_touch_wake);
  } else {
    s_enabled = false;
    exti_disable(s_interrupt);
    (void)xSemaphoreGive(s_touch_wake);
    if (s_finger_down) {
      touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
      s_finger_down = false;
    }
    prv_reset(false);
  }
}
