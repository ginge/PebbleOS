/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/panel.h>
#include <pbl/drivers/spi.h>
#include <pbl/kernel/mutex.h>
struct pbl_panel_co5300 {
  struct pbl_panel panel;
  struct pbl_spi_dev spi;
  struct pbl_gpio reset;
};
extern const struct pbl_panel_co5300 CO5300;
extern const struct pbl_panel_ops pbl_panel_co5300_ops;
int pbl_panel_co5300_init(const struct pbl_device *dev);
int pbl_panel_co5300_set_brightness(uint8_t level);
extern struct pbl_mutex co5300_lock;
