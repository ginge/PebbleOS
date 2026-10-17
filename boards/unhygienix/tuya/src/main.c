/* SPDX-License-Identifier: Apache-2.0 */
#include "hardware.h"
#include "input_port.h"
#include "tal_api.h"
#include "tkl_output.h"

static void prv_button(void *context, ButtonId id, bool pressed) {
  (void)context;
  PR_DEBUG("button %u %s", id, pressed ? "down" : "up");
}
static void prv_touch(void *context, const TouchEvent *event) {
  (void)context;
  PR_DEBUG("touch %u (%d,%d)", event->type, event->x, event->y);
}
static TuyaInputState s_inputs;
static const TuyaInputSink s_sink = {
    .button = prv_button, .touch = prv_touch, .swipe_buttons = true};

static THREAD_HANDLE s_thread;
static uint8_t s_canvas[TUYA_LOGICAL_SIZE * TUYA_LOGICAL_SIZE];

static void prv_draw(const TuyaInputs *input) {
  for (unsigned y = 0; y < TUYA_LOGICAL_SIZE; ++y) {
    for (unsigned x = 0; x < TUYA_LOGICAL_SIZE; ++x) {
      /* Red, green, blue quadrants make panel order and orientation visible. */
      uint8_t color = y < 130 ? (x < 130 ? 0xf0 : 0xcc) : (x < 130 ? 0xc3 : 0xff);
      if (input->touched && x + 3 >= input->x && x <= input->x + 3 && y + 3 >= input->y &&
          y <= input->y + 3) {
        color = 0xc0;
      }
      if (y > 245 && ((x < 130 && input->back) || (x >= 130 && input->select))) {
        color = 0xc0;
      }
      s_canvas[y * TUYA_LOGICAL_SIZE + x] = color;
    }
  }
}

static void prv_run(void *arg) {
  (void)arg;
  tal_log_init(TAL_LOG_LEVEL_INFO, 4096, (TAL_LOG_OUTPUT_CB)tkl_log_output);
  PR_NOTICE("Unhygienix hardware bring-up; PebbleOS kernel is NOT running");
  OPERATE_RET rt = tuya_hardware_init();
  if (rt != OPRT_OK) {
    PR_ERR("hardware init failed: %d; reboot required", rt);
    goto stopped;
  }
  uint32_t tick = 0;
  for (;;) {
    TuyaInputs inputs = {0};
    rt = tuya_hardware_inputs(&inputs, tal_system_get_millisecond());
    if (rt != OPRT_OK) {
      PR_ERR("input read failed: %d; stopping bring-up", rt);
      break;
    }
    tuya_input_process(&s_inputs, &s_sink, inputs.touched, inputs.x, inputs.y, inputs.back,
                       inputs.select, tal_system_get_millisecond());
    prv_draw(&inputs);
    rt = tuya_hardware_present(s_canvas, sizeof(s_canvas));
    if (rt != OPRT_OK) {
      PR_ERR("display failed: %d; reboot required", rt);
      break;
    }
    if ((tick++ % 50) == 0) {
      TuyaBatteryReading battery;
      if (tuya_hardware_battery_read(&battery, NULL) == OPRT_OK) {
        PR_INFO("battery raw=%ld (uncalibrated)", (long)battery.raw_peak);
      }
      float x, y, z;
      TuyaDateTime time;
      if (tuya_hardware_accel_read(&x, &y, &z) == OPRT_OK) {
        PR_INFO("accel mg: %d %d %d", (int)x, (int)y, (int)z);
      }
      rt = tuya_hardware_rtc_read(&time);
      if (rt == OPRT_OK) {
        PR_INFO("RTC %04u-%02u-%02u %02u:%02u:%02u", time.year, time.month, time.day, time.hour,
                time.minute, time.second);
      } else {
        PR_DEBUG("RTC unavailable or unset: %d", rt);
      }
      PR_INFO("charging=%d back=%d select=%d touch=%d (%u,%u)", inputs.charging, inputs.back,
              inputs.select, inputs.touched, inputs.x, inputs.y);
    }
    tal_system_sleep(20);
  }
stopped:
  /* Keep the power latch and any in-flight DMA buffer alive until reset. */
  for (;;) {
    tal_system_sleep(1000);
  }
}

void tuya_app_main(void) {
  THREAD_CFG_T config = {0};
  config.stackDepth = 8192;
  config.priority = THREAD_PRIO_1;
  config.thrdname = "pebble_hw";
  OPERATE_RET rt = tal_thread_create_and_start(&s_thread, NULL, NULL, prv_run, NULL, &config);
  if (rt != OPRT_OK) {
    tkl_log_output("pebble_hw task creation failed\r\n");
  }
}
