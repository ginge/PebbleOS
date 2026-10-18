/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "hardware.h"
#include "rtc_port.h"
#include "tkl_adc.h"
#include <string.h>
#include "tal_api.h"
#include "tkl_gpio.h"
#include "tkl_i2c.h"
#include "tdd_disp_co5300.h"
#include "tdd_tp_cst92xx.h"
#include "tdl_display_manage.h"
#include "tdl_tp_manage.h"
#include "qmi8658.h"

#define PANEL_NAME "pebble-tuya"
#define RTC_ADDRESS 0x51
#define POWER_ENABLE TUYA_GPIO_NUM_19
#define POWER_KEY TUYA_GPIO_NUM_18
#define USER_KEY TUYA_GPIO_NUM_12
#define CHARGING TUYA_GPIO_NUM_30
#define SPEAKER_ENABLE TUYA_GPIO_NUM_28
#define BUS TUYA_I2C_NUM_0

static TDL_DISP_HANDLE_T s_display;
static TDL_TP_HANDLE_T s_touch;
static TDL_DISP_FRAME_BUFF_T *s_frame;
static TDL_DISP_DEV_INFO_T s_info;
static SEM_HANDLE s_frame_done;
static MUTEX_HANDLE s_bus_lock;
static bool s_started, s_ready, s_inflight, s_imu_ready;
static TuyaButton s_back, s_select;

static OPERATE_RET prv_pin(TUYA_GPIO_NUM_E pin, bool output, TUYA_GPIO_LEVEL_E level) {
  TUYA_GPIO_BASE_CFG_T cfg = {0};
  cfg.mode = output ? TUYA_GPIO_PUSH_PULL : TUYA_GPIO_PULLUP;
  cfg.direct = output ? TUYA_GPIO_OUTPUT : TUYA_GPIO_INPUT;
  cfg.level = level;
  return tkl_gpio_init(pin, &cfg);
}

/* The vendor QMI8658 driver needs these app-provided bus operations. */
OPERATE_RET dev_i2c_read_nbytes(uint8_t address, uint8_t reg, uint8_t *data, uint32_t size) {
  if (!s_bus_lock || !data || !size) {
    return OPRT_INVALID_PARM;
  }
  OPERATE_RET rt = tal_mutex_lock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tkl_i2c_master_send(BUS, address, &reg, 1, FALSE);
  if (rt == OPRT_OK) {
    rt = tkl_i2c_master_receive(BUS, address, data, size, FALSE);
  }
  tal_mutex_unlock(s_bus_lock);
  return rt;
}

OPERATE_RET dev_i2c_write(uint8_t address, uint8_t reg, uint8_t value) {
  if (!s_bus_lock) {
    return OPRT_INVALID_PARM;
  }
  uint8_t bytes[] = {reg, value};
  OPERATE_RET rt = tal_mutex_lock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tkl_i2c_master_send(BUS, address, bytes, sizeof(bytes), FALSE);
  tal_mutex_unlock(s_bus_lock);
  return rt;
}

static void prv_frame_done(TDL_DISP_FRAME_BUFF_T *frame) {
  (void)frame;
  tal_semaphore_post(s_frame_done);
}

OPERATE_RET tuya_hardware_init(void) {
  if (s_started) {
    return OPRT_COM_ERROR;
  }
  s_started = true;
  OPERATE_RET rt;
  /* Keep power latched; speaker amplifier is active LOW on this board. */
  TUYA_CALL_ERR_RETURN(prv_pin(POWER_ENABLE, true, TUYA_GPIO_LEVEL_HIGH));
  TUYA_CALL_ERR_RETURN(prv_pin(SPEAKER_ENABLE, true, TUYA_GPIO_LEVEL_HIGH));
  TUYA_CALL_ERR_RETURN(prv_pin(POWER_KEY, false, TUYA_GPIO_LEVEL_HIGH));
  TUYA_CALL_ERR_RETURN(prv_pin(USER_KEY, false, TUYA_GPIO_LEVEL_HIGH));
  TUYA_CALL_ERR_RETURN(prv_pin(CHARGING, false, TUYA_GPIO_LEVEL_HIGH));
  TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&s_bus_lock));
  TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&s_frame_done, 0, 1));

  DISP_QSPI_DEVICE_CFG_T panel = {0};
  panel.width = TUYA_PANEL_SIZE;
  panel.height = TUYA_PANEL_SIZE;
  panel.x_offset = 6;
  panel.pixel_fmt = TUYA_PIXEL_FMT_RGB565;
  panel.rotation = TUYA_DISPLAY_ROTATION_0;
  panel.port = TUYA_QSPI_NUM_0;
  panel.spi_clk = 80000000;
  panel.rst_pin = TUYA_GPIO_NUM_29;
  panel.power.pin = TUYA_GPIO_NUM_MAX;
  panel.bl.type = TUYA_DISP_BL_TP_CUSTOM;
  TUYA_CALL_ERR_RETURN(tdd_disp_qspi_co5300_register(PANEL_NAME, &panel));
  TUYA_CALL_ERR_RETURN(
      tdl_disp_custom_backlight_register(PANEL_NAME, tdd_qspi_co5300_send_cmd_set_bl, NULL));
  s_display = tdl_disp_find_dev(PANEL_NAME);
  if (!s_display) {
    return OPRT_COM_ERROR;
  }
  TUYA_CALL_ERR_RETURN(tdl_disp_dev_get_info(s_display, &s_info));
  TUYA_CALL_ERR_RETURN(tdl_disp_dev_open(s_display));
  TUYA_CALL_ERR_RETURN(tdl_disp_set_brightness(s_display, 20));
  s_frame = tdl_disp_create_frame_buff(DISP_FB_TP_PSRAM, TUYA_FRAME_BYTES);
  if (!s_frame) {
    return OPRT_MALLOC_FAILED;
  }
  s_frame->fmt = TUYA_PIXEL_FMT_RGB565;
  s_frame->width = TUYA_PANEL_SIZE;
  s_frame->height = TUYA_PANEL_SIZE;
  s_frame->x_start = 0;
  s_frame->y_start = 0;
  s_frame->free_cb = prv_frame_done;

  TDD_TP_CST92XX_INFO_T touch = {0};
  touch.rst_pin = TUYA_GPIO_NUM_42;
  touch.i2c_cfg.port = BUS;
  touch.i2c_cfg.scl_pin = TUYA_GPIO_NUM_20;
  touch.i2c_cfg.sda_pin = TUYA_GPIO_NUM_21;
  touch.tp_cfg.x_max = TUYA_PANEL_SIZE;
  touch.tp_cfg.y_max = TUYA_PANEL_SIZE;
  /* Apply mirroring ourselves with size-1, and reject out-of-range reports. */
  TUYA_CALL_ERR_RETURN(tdd_tp_i2c_cst92xx_register(PANEL_NAME, &touch));
  s_touch = tdl_tp_find_dev(PANEL_NAME);
  if (!s_touch) {
    return OPRT_COM_ERROR;
  }
  TUYA_CALL_ERR_RETURN(tdl_tp_dev_open(s_touch));

  rt = qmi8658_init(&g_qmi8658_dev, QMI8658_ADDRESS_HIGH);
  if (rt == OPRT_OK) {
    rt = qmi8658_enable_sensors(&g_qmi8658_dev, QMI8658_DISABLE_ALL);
  }
  if (rt == OPRT_OK) {
    rt = qmi8658_set_accel_range(&g_qmi8658_dev, QMI8658_ACCEL_RANGE_4G);
  }
  if (rt == OPRT_OK) {
    rt = qmi8658_set_accel_odr(&g_qmi8658_dev, QMI8658_ACCEL_ODR_125HZ);
  }
  if (rt == OPRT_OK) {
    rt = qmi8658_enable_sensors(&g_qmi8658_dev, QMI8658_ENABLE_ACCEL);
  }
  s_imu_ready = rt == OPRT_OK;
  if (!s_imu_ready) {
    PR_WARN("QMI8658 unavailable (%d)", rt);
  }
  s_ready = true;
  return OPRT_OK;
}

OPERATE_RET tuya_hardware_present(const uint8_t *frame, size_t size) {
  if (!s_ready || s_inflight) {
    return OPRT_COM_ERROR;
  }
  if (!tuya_expand_frame(frame, size, s_frame->frame, s_frame->len, s_info.is_swap)) {
    return OPRT_INVALID_PARM;
  }
  s_inflight = true;
  OPERATE_RET rt = tdl_disp_dev_flush(s_display, s_frame);
  if (rt != OPRT_OK) {
    s_inflight = false;
    return rt;
  }
  rt = tal_semaphore_wait(s_frame_done, 2000);
  if (rt == OPRT_OK) {
    s_inflight = false;
  }
  /* On timeout the DMA may still own the buffer. Require reboot, never reuse. */
  return rt;
}

OPERATE_RET tuya_hardware_brightness(uint8_t percent) {
  if (!s_ready || s_inflight || percent > 100) {
    return OPRT_INVALID_PARM;
  }
  /* Vendor callback clamps zero to 5/255: this is dimming, not panel sleep. */
  return tdl_disp_set_brightness(s_display, percent);
}

OPERATE_RET tuya_hardware_inputs(TuyaInputs *inputs, uint32_t now_ms) {
  if (!s_ready || !inputs) {
    return OPRT_INVALID_PARM;
  }
  TuyaInputs sample = {0};
  TDL_TP_POS_T point;
  uint8_t count = 0;
  OPERATE_RET rt = tal_mutex_lock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tdl_tp_dev_read(s_touch, 1, &point, &count);
  tal_mutex_unlock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  if (count) {
    if (!tuya_map_touch(point.x, point.y, &sample.x, &sample.y)) {
      return OPRT_COM_ERROR;
    }
    sample.touched = true;
  }
  TUYA_GPIO_LEVEL_E back, select, charging;
  TUYA_CALL_ERR_RETURN(tkl_gpio_read(POWER_KEY, &back));
  TUYA_CALL_ERR_RETURN(tkl_gpio_read(USER_KEY, &select));
  TUYA_CALL_ERR_RETURN(tkl_gpio_read(CHARGING, &charging));
  tuya_debounce(&s_back, back == TUYA_GPIO_LEVEL_LOW, now_ms);
  tuya_debounce(&s_select, select == TUYA_GPIO_LEVEL_LOW, now_ms);
  sample.back = s_back.stable;
  sample.select = s_select.stable;
  sample.charging = charging == TUYA_GPIO_LEVEL_LOW;
  *inputs = sample;
  return OPRT_OK;
}

OPERATE_RET tuya_hardware_rtc_read(TuyaDateTime *time) {
  if (!s_ready || !time) {
    return OPRT_INVALID_PARM;
  }
  uint8_t control, registers[7];
  OPERATE_RET rt = dev_i2c_read_nbytes(RTC_ADDRESS, 0, &control, 1);
  if (rt != OPRT_OK) {
    return rt;
  }
  /* Do not silently decode stopped clocks or 12-hour mode as 24-hour time. */
  if (control & 0x22) {
    return OPRT_COM_ERROR;
  }
  TUYA_CALL_ERR_RETURN(dev_i2c_read_nbytes(RTC_ADDRESS, 4, registers, sizeof(registers)));
  return tuya_decode_rtc(registers, time) ? OPRT_OK : OPRT_COM_ERROR;
}

OPERATE_RET tuya_hardware_accel_read(float *x_mg, float *y_mg, float *z_mg) {
  if (!s_ready || !s_imu_ready || !x_mg || !y_mg || !z_mg) {
    return OPRT_INVALID_PARM;
  }
  return qmi8658_read_accel(&g_qmi8658_dev, x_mg, y_mg, z_mg);
}

static int prv_rtc_bus_read(void *context, uint8_t reg, uint8_t *data, size_t size) {
  (void)context;
  OPERATE_RET rt = tkl_i2c_master_send(BUS, RTC_ADDRESS, &reg, 1, FALSE);
  if (rt == OPRT_OK) {
    rt = tkl_i2c_master_receive(BUS, RTC_ADDRESS, data, size, FALSE);
  }
  return rt;
}
static int prv_rtc_bus_write(void *context, uint8_t reg, const uint8_t *data, size_t size) {
  (void)context;
  if (size > 7) {
    return OPRT_INVALID_PARM;
  }
  uint8_t bytes[8];
  bytes[0] = reg;
  memcpy(bytes + 1, data, size);
  return tkl_i2c_master_send(BUS, RTC_ADDRESS, bytes, size + 1, FALSE);
}
static const TuyaRtcBus s_rtc_bus = {.read = prv_rtc_bus_read, .write = prv_rtc_bus_write};

OPERATE_RET tuya_hardware_rtc_set(const TuyaDateTime *time) {
  if (!s_ready) {
    return OPRT_COM_ERROR;
  }
  OPERATE_RET rt = tal_mutex_lock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tuya_rtc_set(&s_rtc_bus, time);
  tal_mutex_unlock(s_bus_lock);
  return rt;
}
OPERATE_RET tuya_hardware_rtc_daily_alarm(uint8_t hour, uint8_t minute, uint8_t second) {
  if (!s_ready) {
    return OPRT_COM_ERROR;
  }
  OPERATE_RET rt = tal_mutex_lock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tuya_rtc_daily_alarm(&s_rtc_bus, hour, minute, second);
  tal_mutex_unlock(s_bus_lock);
  return rt;
}
OPERATE_RET tuya_hardware_rtc_alarm_clear(void) {
  if (!s_ready) {
    return OPRT_COM_ERROR;
  }
  OPERATE_RET rt = tal_mutex_lock(s_bus_lock);
  if (rt != OPRT_OK) {
    return rt;
  }
  rt = tuya_rtc_alarm_clear(&s_rtc_bus);
  tal_mutex_unlock(s_bus_lock);
  return rt;
}

OPERATE_RET tuya_hardware_battery_read(TuyaBatteryReading *reading,
                                       const TuyaBatteryCalibration *calibration) {
  if (!s_ready || !reading) {
    return OPRT_INVALID_PARM;
  }
  static bool adc_ready;
  if (!adc_ready) {
    TUYA_ADC_BASE_CFG_T cfg = {0};
    cfg.ch_list.data = 1u << 15; /* P13, battery divider tap. */
    cfg.ch_nums = 1;
    cfg.width = 12;
    cfg.mode = TUYA_ADC_CONTINUOUS;
    cfg.type = TUYA_ADC_INNER_SAMPLE_VOL;
    cfg.conv_cnt = 1;
    OPERATE_RET rt = tkl_adc_init(TUYA_ADC_NUM_0, &cfg);
    if (rt != OPRT_OK) {
      return rt;
    }
    adc_ready = true;
  }
  int32_t peak = -1;
  for (unsigned i = 0; i < 16; ++i) {
    int32_t raw;
    OPERATE_RET rt = tkl_adc_read_single_channel(TUYA_ADC_NUM_0, 15, &raw);
    if (rt != OPRT_OK) {
      return rt;
    }
    if (raw < 0 || raw > 65535) {
      return OPRT_COM_ERROR;
    }
    if (raw > peak) {
      peak = raw;
    }
  }
  TuyaBatteryReading sample = {.raw_peak = peak};
  if (calibration) {
    if (!tuya_battery_millivolts(peak, calibration, &sample.millivolts)) {
      return OPRT_INVALID_PARM;
    }
    sample.calibrated = true;
  }
  *reading = sample;
  return OPRT_OK;
}
