/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/drivers/touch.h>
#include <pbl/kernel/work.h>

struct pbl_cst92xx_t5ai;
struct pbl_cst92xx_t5ai_state {
  const struct pbl_cst92xx_t5ai *cst;
  struct pbl_mutex i2c_lock;
  struct pbl_work touch_work;
  struct pbl_delayed_work retry_timer;
  struct pbl_delayed_work watchdog_timer;
  uint32_t generation;
  uint32_t work_generation;
  uint32_t retry_generation;
  uint32_t read_failures;
#ifdef CONFIG_T5AI_IO_PROFILE
  uint32_t queued_at;
#endif
  uint16_t idle_poll_ms;
  // Cached scaled coordinates, before the touch class applies orientation.
  int16_t last_x;
  int16_t last_y;
  uint8_t consecutive_read_failures;
  bool enabled;
  bool initialized;
  bool down;
  bool contact_active;
  bool palm_down;
  bool callback_scheduled;
  bool retry_pending;
  bool poll_scheduled;
  bool transfer_retry;
};
// The native touch class remains the public front end of this driver device.
struct pbl_cst92xx_t5ai {
  struct pbl_touch touch;
  struct pbl_cst92xx_t5ai_state *state;
  struct pbl_i2c_dev i2c;
  struct pbl_irq_spec interrupt;
  struct pbl_gpio interrupt_input;
  struct pbl_gpio reset;
  // Controller resolution before scaling to touch.props dimensions.
  uint16_t native_width;
  uint16_t native_height;
};
#define PBL_CST92XX_T5AI_STATE_DEFINE(sym) \
  PBL_TOUCH_STATE_DEFINE(sym);             \
  static struct pbl_cst92xx_t5ai_state sym##_cst92xx_state = {.i2c_lock = PBL_MUTEX_INITIALIZER}
extern const struct pbl_touch_ops pbl_touch_cst92xx_t5ai_ops;
