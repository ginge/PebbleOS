/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include <pbl/drivers/spi/t5ai.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/timer.h>
#include <pbl/mcu/cache.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/util/misc.h>

#include <cmsis_core.h>

#define REG(o) (*(volatile uint32_t *)(0x46040000u + (o)))
#define SYS(o) (*(volatile uint32_t *)(0x44010000u + (o)))
#define STREAM ((1u << 22) | (1u << 16))
static const struct pbl_spi_bus *s_bus;
static int s_status;
static bool s_stream;
static bool s_command_pending;
static size_t s_offset;
static struct pbl_timer s_timeout;
static bool s_dma_active;
static void prv_expired(struct pbl_timer *timer);
static const struct pbl_spi_t5ai *prv_dev(const struct pbl_spi_bus *bus) {
  return container_of(bus, const struct pbl_spi_t5ai, bus);
}
static void prv_done(void *arg, int status);
static int prv_dma_next(const struct pbl_spi_bus *bus) {
  const struct pbl_spi_xfer *xfer = &bus->state->xfers[bus->state->idx];
  const size_t remaining = xfer->len - s_offset;
  const size_t count = remaining > 65536u ? 65536u : remaining;
  void *buffer = (uint8_t *)xfer->tx_buf + s_offset;
  dcache_flush(buffer, count);
  pbl_irq_lock();
  s_dma_active = true;
  const int ret = pbl_dma_prep_slave_single(&prv_dev(bus)->dma, buffer, count, PBL_DMA_MEM_TO_DEV,
                                            prv_done, (void *)bus);
  if (ret == 0)
    pbl_timer_start(&s_timeout, PBL_MSEC(20));
  else
    s_dma_active = false;
  pbl_irq_unlock();
  return ret;
}
static void prv_done(void *arg, int status) {
  const struct pbl_spi_bus *bus = arg;
  if (!s_dma_active)
    return;
  s_dma_active = false;
  pbl_timer_stop(&s_timeout);
  const struct pbl_spi_xfer *xfer = &bus->state->xfers[bus->state->idx];
  if (status == 0) {
    const size_t remaining = xfer->len - s_offset;
    s_offset += remaining > 65536u ? 65536u : remaining;
    if (s_offset < xfer->len) {
      status = prv_dma_next(bus);
      if (status == 0)
        return;
    }
  }
  if (status == 0) {
    // DMA completion only hands bytes to QSPI; wait until the last bits leave the pins.
    status = -ETIMEDOUT;
    for (unsigned us = 0; us < 1000u; ++us) {
      const uint32_t wire = REG(0x70);
      if (!(wire & (1u << 14)) && (wire & (1u << 16))) {
        status = 0;
        break;
      }
      delay_us(1);
    }
  }
  pbl_spi_bus_complete(bus, status);
}
static void prv_expired(struct pbl_timer *timer) {
  (void)timer;
  pbl_irq_lock();
  if (s_dma_active) {
    s_dma_active = false;
    (void)pbl_dma_terminate(&prv_dev(s_bus)->dma);
    pbl_spi_bus_complete(s_bus, -ETIMEDOUT);
  }
  pbl_irq_unlock();
}
void pbl_spi_t5ai_irq_handler(void) {
  if (!s_command_pending)
    return;
  s_command_pending = false;
  pbl_spi_bus_complete(s_bus, s_status);
}
static int prv_init(const struct pbl_spi_bus *bus) {
  s_bus = bus;
  pbl_timer_init(&s_timeout, prv_expired);
  (void)t5ai_cp_mailbox_enable_qspi0();
  SYS(0x38) &= ~(1u << 6);
  SYS(0x3c) &= ~(1u << 6);
  SYS(0x30) |= 1u << 20;
  delay_us(10);
  int ret = pbl_pinctrl_select_state(&prv_dev(bus)->pinctrl, PBL_PINCTRL_STATE_DEFAULT);
  if (ret != 0)
    return ret;
  ret = pbl_device_init(&prv_dev(bus)->dma.ctrl->dev);
  if (ret != 0)
    return ret;
  const struct pbl_dma_slave_config cfg = {
    .dst_addr = 0x64000000u,
    .dst_addr_width = PBL_DMA_SLAVE_BUSWIDTH_4_BYTES,
  };
  ret = pbl_dma_slave_config(&prv_dev(bus)->dma, &cfg);
  if (ret != 0)
    return ret;
  REG(0x60) = (8u << 24) | 1u | (1u << 3);
  REG(0x20) = REG(0x24) = 0;
  REG(0x28) = 3;
  REG(0x2c) = 2u << 14;
  REG(0x64) = 0;
  REG(8) |= 1u;
  NVIC_SetPriority(QSPI0_IRQn, 6);
  NVIC_EnableIRQ(QSPI0_IRQn);
  return 0;
}
static int prv_configure(const struct pbl_spi_bus *bus, const struct pbl_spi_dev *dev) {
  (void)bus;
  if (dev->mode != PBL_SPI_MODE_0 || dev->max_hz < 30000000u)
    return -ENOTSUP;
  unsigned divider = (480000000u + dev->max_hz - 1u) / dev->max_hz;
  if (divider < 1u)
    divider = 1u;
  if (divider > 16u)
    return -ENOTSUP;
  SYS(0x24) = (SYS(0x24) & ~((15u << 6) | (1u << 10))) | ((divider - 1u) << 6) | (1u << 10);
  return 0;
}
static void prv_enable(const struct pbl_spi_bus *bus) {
  (void)bus;
  s_stream = false;
  REG(0x60) |= 1u << 6;
}
static void prv_disable(const struct pbl_spi_bus *bus) {
  (void)bus;
  // Restore command clocks before releasing the memory aperture and CS.
  REG(0x60) &= ~(1u << 16);
  REG(0x60) &= ~(1u << 22);
  REG(0x60) &= ~(1u << 6);
  s_stream = false;
}
static int prv_start(const struct pbl_spi_bus *bus, const struct pbl_spi_xfer *xfer) {
  if (!xfer->tx_buf || xfer->rx_buf)
    return -ENOTSUP;
  if (xfer->tx_nbits == 4u) {
    if (!s_stream || ((uintptr_t)xfer->tx_buf & 3u) || (xfer->len & 3u))
      return -EINVAL;
    s_offset = 0;
    return prv_dma_next(bus);
  }
  // Command-C has a four-byte instruction/address phase and a 256-byte FIFO.
  if (s_stream || xfer->tx_nbits > 1u || xfer->len < 4u || xfer->len > 260u)
    return -ENOTSUP;
  const uint8_t *bytes = xfer->tx_buf;
  uint32_t header;
  memcpy(&header, bytes, 4);
  REG(0x40) = 0;
  REG(0x44) = header;
  REG(0x48) = REG(0x4c) = 0;
  const size_t length = xfer->len - 4u;
  for (size_t i = 0; i < length; i += 4u) {
    uint32_t word = 0;
    memcpy(&word, bytes + 4u + i, length - i < 4u ? length - i : 4u);
    REG(0x100 + i) = word;
  }
  const bool quad = bus->state->idx + 1u < bus->state->num_xfers &&
                    bus->state->xfers[bus->state->idx + 1u].tx_nbits == 4u;
  REG(0x48) = 0x300u;
  REG(0x4c) = (length << 2) | (quad ? 2u << 14 : 0u) | 1u;
  s_status = -ETIMEDOUT;
  for (unsigned i = 0; i < 10000u; ++i) {
    if (REG(0x70) & (1u << 2)) {
      REG(0x6c) = 1u << 2;
      REG(0x6c) = 0;
      s_status = 0;
      break;
    }
    delay_us(1);
  }
  REG(0x48) = REG(0x4c) = 0;
  if (s_status == 0 && quad) {
    REG(0x60) |= STREAM;
    s_stream = true;
  }
  s_command_pending = true;
  NVIC_SetPendingIRQ(QSPI0_IRQn);
  return 0;
}
const struct pbl_spi_bus_ops pbl_spi_t5ai_ops = {
  .init = prv_init,
  .configure = prv_configure,
  .enable = prv_enable,
  .disable = prv_disable,
  .start = prv_start,
};
