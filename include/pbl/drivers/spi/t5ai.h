/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/dma.h>
#include <pbl/drivers/pinctrl.h>
#include <pbl/drivers/spi.h>
struct pbl_spi_t5ai {
  struct pbl_spi_bus bus;
  struct pbl_pinctrl pinctrl;
  struct pbl_dma_chan dma;
};
extern const struct pbl_spi_bus_ops pbl_spi_t5ai_ops;
void pbl_spi_t5ai_irq_handler(void);
