/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/panel.h>
#include <pbl/drivers/spi.h>
#include <pbl/kernel/mutex.h>
enum pbl_panel_co5300_display_state {
  CO5300_DISPLAY_UNKNOWN,
  CO5300_DISPLAY_OFF,
  CO5300_DISPLAY_ON,
};
struct pbl_panel_co5300_state {
  struct pbl_mutex lock;
  uint8_t brightness;
  uint8_t applied_brightness;
  bool prepared;
  bool brightness_valid;
  enum pbl_panel_co5300_display_state display;
};
struct pbl_panel_co5300 {
  struct pbl_panel panel;
  struct pbl_spi_dev spi;
  struct pbl_gpio reset;
  struct pbl_panel_co5300_state *state;
};
extern const struct pbl_panel_co5300 CO5300;
extern const struct pbl_panel_ops pbl_panel_co5300_ops;
int pbl_panel_co5300_init(const struct pbl_device *dev);
int pbl_panel_co5300_set_brightness(const struct pbl_panel *panel, uint8_t level);
#define PBL_PANEL_CO5300_STATE_DEFINE(sym) \
  PBL_DEVICE_STATE_DEFINE(sym);            \
  static struct pbl_panel_co5300_state sym##_panel_state = {.lock = PBL_MUTEX_INITIALIZER}
