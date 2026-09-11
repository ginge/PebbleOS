/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/touch/touch_sensor.h>

#include "board/board.h"
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/os/tick.h>
#include "kernel/util/sleep.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/system_task.h"
#include "pbl/services/touch/touch.h"
#include <pbl/logging/logging.h>

PBL_LOG_MODULE_DEFINE(driver_touch_cst92xx, CONFIG_DRIVER_TOUCH_LOG_LEVEL);

#define CST92XX_RESET_PIN 42u
#define CST92XX_DATA_REGISTER 0xd000u
#define CST92XX_REPORT_LENGTH 10u
#define CST92XX_REPORT_ACK 0xabu
#define CST92XX_TOUCH_STATUS 0x06u
#define CST92XX_NATIVE_SIZE 466u
#define CST92XX_POLL_MS 20u

static TimerID s_poll_timer = TIMER_INVALID_ID;
static bool s_enabled;
static bool s_poll_pending;
static bool s_finger_down;
static int16_t s_last_x;
static int16_t s_last_y;
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

static int16_t prv_map_coordinate(uint16_t coordinate) {
  return (int16_t)(coordinate * PBL_DISPLAY_WIDTH / CST92XX_NATIVE_SIZE);
}

static void prv_poll(void *context) {
  (void)context;
  s_poll_pending = false;
  if (!s_enabled) {
    return;
  }

  uint8_t report[CST92XX_REPORT_LENGTH] = {0};
  if (!prv_read_report(report)) {
    PBL_LOG_ERR("CST92xx report read failed");
    return;
  }

  const uint8_t points = report[5] & 0x7fu;
  const uint8_t status = report[0] & 0x0fu;
  if (report[6] == CST92XX_REPORT_ACK && points != 0u && status == CST92XX_TOUCH_STATUS) {
    const uint16_t native_x = ((uint16_t)report[1] << 4) | (report[3] >> 4);
    const uint16_t native_y = ((uint16_t)report[2] << 4) | (report[3] & 0x0fu);
    if (native_x < CST92XX_NATIVE_SIZE && native_y < CST92XX_NATIVE_SIZE) {
      s_last_x = prv_map_coordinate(native_x);
      s_last_y = prv_map_coordinate(native_y);
      touch_handle_update(TouchState_FingerDown, s_last_x, s_last_y);
      s_finger_down = true;
      return;
    }
    PBL_LOG_WRN("CST92xx coordinate out of range: %u,%u", (unsigned)native_x, (unsigned)native_y);
  }

  if (s_finger_down) {
    touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
    s_finger_down = false;
  }
}

static void prv_timer_callback(void *context) {
  (void)context;
  if (!s_enabled || s_poll_pending) {
    return;
  }
  s_poll_pending = true;
  if (!system_task_add_callback(prv_poll, NULL)) {
    s_poll_pending = false;
  }
}

void touch_sensor_init(void) {
  gpio_output_init(&s_reset, GPIO_OType_PP);
  prv_reset(true);
  uint8_t report[CST92XX_REPORT_LENGTH];
  if (!prv_read_report(report)) {
    PBL_LOG_ERR("CST92xx not detected at 0x5a");
  }
  s_poll_timer = new_timer_create();
  if (s_poll_timer == TIMER_INVALID_ID) {
    PBL_LOG_ERR("Could not create CST92xx poll timer");
  }
  s_enabled = true;
  touch_sensor_set_enabled(false);
}

void touch_sensor_set_enabled(bool enabled) {
  if (s_enabled == enabled) {
    return;
  }
  s_enabled = enabled;
  s_poll_pending = false;
  if (enabled) {
    prv_reset(true);
    if (s_poll_timer != TIMER_INVALID_ID) {
      (void)new_timer_start(s_poll_timer, CST92XX_POLL_MS, prv_timer_callback, NULL,
                            TIMER_START_FLAG_REPEATING);
    }
  } else {
    if (s_poll_timer != TIMER_INVALID_ID) {
      (void)new_timer_stop(s_poll_timer);
    }
    if (s_finger_down) {
      touch_handle_update(TouchState_FingerUp, s_last_x, s_last_y);
      s_finger_down = false;
    }
    prv_reset(false);
  }
}
