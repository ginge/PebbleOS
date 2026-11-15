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

// Byte offsets and fields from the pinned BK7258 AP qspi_struct.h/sys_struct.h.
#define QSPI_BASE            0x46040000u
#define QSPI_REG(offset)     (*(volatile uint32_t *)(QSPI_BASE + (offset)))
#define QSPI_GLOBAL_CONTROL  0x08u
#define QSPI_COMMAND_A_LOW   0x20u
#define QSPI_COMMAND_A_HIGH  0x24u
#define QSPI_COMMAND_A_LINES 0x28u
#define QSPI_COMMAND_A_DATA  0x2cu
#define QSPI_COMMAND_C_LOW   0x40u
#define QSPI_COMMAND_C_HIGH  0x44u
#define QSPI_COMMAND_C_LINES 0x48u
#define QSPI_COMMAND_C_DATA  0x4cu
#define QSPI_CONFIG          0x60u
#define QSPI_FIFO_RESET      0x64u
#define QSPI_STATUS_CLEAR    0x6cu
#define QSPI_STATUS          0x70u
#define QSPI_COMMAND_FIFO    0x100u
#define QSPI_DMA_APERTURE    0x64000000u

#define QSPI_GLOBAL_RESET_RELEASE         (1u << 0)
#define QSPI_CONFIG_ENABLE                (1u << 0)
#define QSPI_CONFIG_IO2_IO3_MODE          (1u << 3)
#define QSPI_CONFIG_FORCE_CS_LOW          (1u << 6)
#define QSPI_CONFIG_DISABLE_COMMAND_CLOCK (1u << 16)
#define QSPI_CONFIG_MEMORY_SELECT         (1u << 22)
#define QSPI_CONFIG_CS_HIGH_WAIT_SHIFT    24u
#define QSPI_CS_HIGH_WAIT_CYCLES          8u
#define QSPI_STREAM_CONFIG        (QSPI_CONFIG_MEMORY_SELECT | QSPI_CONFIG_DISABLE_COMMAND_CLOCK)
#define QSPI_COMMAND_START        (1u << 0)
#define QSPI_COMMAND_LENGTH_SHIFT 2u
#define QSPI_COMMAND_DATA_LINES_SHIFT 14u
#define QSPI_DATA_QUAD                2u
// Line encoding 3 ends the instruction/address phase at that byte.
#define QSPI_HEADER_END               3u
#define QSPI_HEADER_BYTE5_LINES_SHIFT 8u
#define QSPI_COMMAND_DONE             (1u << 2)
#define QSPI_TRANSMIT_BUSY            (1u << 14)
#define QSPI_FIFO_EMPTY               (1u << 16)

#define SYSTEM_BASE                    0x44010000u
#define SYSTEM_REG(offset)             (*(volatile uint32_t *)(SYSTEM_BASE + (offset)))
#define SYSTEM_QSPI_CLOCK_SELECT       0x24u
#define SYSTEM_PERIPHERAL_CLOCK_ENABLE 0x30u
#define SYSTEM_MEMORY_SHUTDOWN         0x38u
#define SYSTEM_MEMORY_DEEP_SLEEP       0x3cu
#define SYSTEM_QSPI0_MEMORY_POWER_DOWN (1u << 6)
#define SYSTEM_QSPI0_CLOCK_ENABLE      (1u << 20)
#define SYSTEM_QSPI0_DIVIDER_SHIFT     6u
#define SYSTEM_QSPI0_DIVIDER_MASK      (15u << SYSTEM_QSPI0_DIVIDER_SHIFT)
#define SYSTEM_QSPI0_SOURCE_PLL        (1u << 10)

#define QSPI_SOURCE_HZ             480000000u
#define QSPI_MIN_SUPPORTED_HZ      30000000u
#define QSPI_MAX_DIVIDER           16u
#define QSPI_QUAD_LANES            4u
#define QSPI_WORD_BYTES            4u
#define QSPI_HEADER_BYTES          4u
#define QSPI_COMMAND_FIFO_BYTES    256u
#define QSPI_DMA_CHUNK_BYTES       65536u
#define QSPI_DMA_TIMEOUT_MS        20u
#define QSPI_WIRE_DRAIN_TIMEOUT_US 1000u
#define QSPI_COMMAND_TIMEOUT_US    10000u
#define QSPI_POWER_SETTLE_US       10u
#define QSPI_IRQ_PRIORITY          6u

static const struct pbl_spi_bus *s_bus;
static int s_command_status;
static bool s_quad_stream_active;
static bool s_command_pending;
static size_t s_dma_offset;
static struct pbl_timer s_dma_timeout;
static bool s_dma_active;

static void prv_dma_timeout(struct pbl_timer *timer);
static const struct pbl_spi_t5ai *prv_device(const struct pbl_spi_bus *bus) {
  return container_of(bus, const struct pbl_spi_t5ai, bus);
}

static void prv_dma_complete(void *arg, int status);

static int prv_submit_dma_chunk(const struct pbl_spi_bus *bus) {
  const struct pbl_spi_xfer *xfer = &bus->state->xfers[bus->state->idx];
  const size_t remaining = xfer->len - s_dma_offset;
  const size_t count = remaining > QSPI_DMA_CHUNK_BYTES ? QSPI_DMA_CHUNK_BYTES : remaining;
  void *buffer = (uint8_t *)xfer->tx_buf + s_dma_offset;
  dcache_flush(buffer, count);
  pbl_irq_lock();
  s_dma_active = true;
  const int ret = pbl_dma_prep_slave_single(&prv_device(bus)->dma, buffer, count,
                                            PBL_DMA_MEM_TO_DEV, prv_dma_complete, (void *)bus);
  if (ret == 0)
    pbl_timer_start(&s_dma_timeout, PBL_MSEC(QSPI_DMA_TIMEOUT_MS));
  else
    s_dma_active = false;
  pbl_irq_unlock();
  return ret;
}

static void prv_dma_complete(void *arg, int status) {
  const struct pbl_spi_bus *bus = arg;
  if (!s_dma_active)
    return;
  s_dma_active = false;
  pbl_timer_stop(&s_dma_timeout);
  const struct pbl_spi_xfer *xfer = &bus->state->xfers[bus->state->idx];
  if (status == 0) {
    const size_t remaining = xfer->len - s_dma_offset;
    s_dma_offset += remaining > QSPI_DMA_CHUNK_BYTES ? QSPI_DMA_CHUNK_BYTES : remaining;
    if (s_dma_offset < xfer->len) {
      status = prv_submit_dma_chunk(bus);
      if (status == 0)
        return;
    }
  }
  if (status == 0) {
    // DMA completion only hands bytes to QSPI; wait until the last bits leave the pins.
    status = -ETIMEDOUT;
    for (unsigned elapsed_us = 0; elapsed_us < QSPI_WIRE_DRAIN_TIMEOUT_US; ++elapsed_us) {
      const uint32_t wire_status = QSPI_REG(QSPI_STATUS);
      if (!(wire_status & QSPI_TRANSMIT_BUSY) && (wire_status & QSPI_FIFO_EMPTY)) {
        status = 0;
        break;
      }
      delay_us(1);
    }
  }
  pbl_spi_bus_complete(bus, status);
}

static void prv_dma_timeout(struct pbl_timer *timer) {
  (void)timer;
  pbl_irq_lock();
  if (s_dma_active) {
    s_dma_active = false;
    (void)pbl_dma_terminate(&prv_device(s_bus)->dma);
    pbl_spi_bus_complete(s_bus, -ETIMEDOUT);
  }
  pbl_irq_unlock();
}

void pbl_spi_t5ai_irq_handler(void) {
  if (!s_command_pending)
    return;
  s_command_pending = false;
  pbl_spi_bus_complete(s_bus, s_command_status);
}

static int prv_init(const struct pbl_spi_bus *bus) {
  s_bus = bus;
  pbl_timer_init(&s_dma_timeout, prv_dma_timeout);
  const int power_status = t5ai_cp_mailbox_enable_qspi0();
  if (power_status != 0)
    return -EIO;
  SYSTEM_REG(SYSTEM_MEMORY_SHUTDOWN) &= ~SYSTEM_QSPI0_MEMORY_POWER_DOWN;
  SYSTEM_REG(SYSTEM_MEMORY_DEEP_SLEEP) &= ~SYSTEM_QSPI0_MEMORY_POWER_DOWN;
  SYSTEM_REG(SYSTEM_PERIPHERAL_CLOCK_ENABLE) |= SYSTEM_QSPI0_CLOCK_ENABLE;
  delay_us(QSPI_POWER_SETTLE_US);
  int ret = pbl_pinctrl_select_state(&prv_device(bus)->pinctrl, PBL_PINCTRL_STATE_DEFAULT);
  if (ret != 0)
    return ret;
  ret = pbl_device_init(&prv_device(bus)->dma.ctrl->dev);
  if (ret != 0)
    return ret;
  const struct pbl_dma_slave_config cfg = {
    .dst_addr = QSPI_DMA_APERTURE,
    .dst_addr_width = PBL_DMA_SLAVE_BUSWIDTH_4_BYTES,
  };
  ret = pbl_dma_slave_config(&prv_device(bus)->dma, &cfg);
  if (ret != 0)
    return ret;
  QSPI_REG(QSPI_CONFIG) = (QSPI_CS_HIGH_WAIT_CYCLES << QSPI_CONFIG_CS_HIGH_WAIT_SHIFT) |
                          QSPI_CONFIG_ENABLE | QSPI_CONFIG_IO2_IO3_MODE;
  QSPI_REG(QSPI_COMMAND_A_LOW) = QSPI_REG(QSPI_COMMAND_A_HIGH) = 0;
  QSPI_REG(QSPI_COMMAND_A_LINES) = QSPI_HEADER_END;
  QSPI_REG(QSPI_COMMAND_A_DATA) = QSPI_DATA_QUAD << QSPI_COMMAND_DATA_LINES_SHIFT;
  QSPI_REG(QSPI_FIFO_RESET) = 0;
  QSPI_REG(QSPI_GLOBAL_CONTROL) |= QSPI_GLOBAL_RESET_RELEASE;
  NVIC_SetPriority(QSPI0_IRQn, QSPI_IRQ_PRIORITY);
  NVIC_EnableIRQ(QSPI0_IRQn);
  return 0;
}

static int prv_configure(const struct pbl_spi_bus *bus, const struct pbl_spi_dev *dev) {
  (void)bus;
  if (dev->mode != PBL_SPI_MODE_0 || dev->max_hz < QSPI_MIN_SUPPORTED_HZ)
    return -ENOTSUP;
  unsigned divider = (QSPI_SOURCE_HZ + dev->max_hz - 1u) / dev->max_hz;
  if (divider < 1u)
    divider = 1u;
  if (divider > QSPI_MAX_DIVIDER)
    return -ENOTSUP;
  SYSTEM_REG(SYSTEM_QSPI_CLOCK_SELECT) = (SYSTEM_REG(SYSTEM_QSPI_CLOCK_SELECT) &
                                          ~(SYSTEM_QSPI0_DIVIDER_MASK | SYSTEM_QSPI0_SOURCE_PLL)) |
                                         ((divider - 1u) << SYSTEM_QSPI0_DIVIDER_SHIFT) |
                                         SYSTEM_QSPI0_SOURCE_PLL;
  return 0;
}

static void prv_enable(const struct pbl_spi_bus *bus) {
  (void)bus;
  s_quad_stream_active = false;
  QSPI_REG(QSPI_CONFIG) |= QSPI_CONFIG_FORCE_CS_LOW;
}

static void prv_disable(const struct pbl_spi_bus *bus) {
  (void)bus;
  // Restore command clocks before releasing the memory aperture and CS.
  QSPI_REG(QSPI_CONFIG) &= ~QSPI_CONFIG_DISABLE_COMMAND_CLOCK;
  QSPI_REG(QSPI_CONFIG) &= ~QSPI_CONFIG_MEMORY_SELECT;
  QSPI_REG(QSPI_CONFIG) &= ~QSPI_CONFIG_FORCE_CS_LOW;
  s_quad_stream_active = false;
}

static int prv_start(const struct pbl_spi_bus *bus, const struct pbl_spi_xfer *xfer) {
  if (!xfer->tx_buf || xfer->rx_buf)
    return -ENOTSUP;
  if (xfer->tx_nbits == QSPI_QUAD_LANES) {
    if (!s_quad_stream_active || ((uintptr_t)xfer->tx_buf & (QSPI_WORD_BYTES - 1u)) ||
        (xfer->len & (QSPI_WORD_BYTES - 1u)))
      return -EINVAL;
    s_dma_offset = 0;
    return prv_submit_dma_chunk(bus);
  }
  // Command-C has a four-byte instruction/address phase and a 256-byte FIFO.
  if (s_quad_stream_active || xfer->tx_nbits > 1u || xfer->len < QSPI_HEADER_BYTES ||
      xfer->len > QSPI_HEADER_BYTES + QSPI_COMMAND_FIFO_BYTES)
    return -ENOTSUP;
  const uint8_t *bytes = xfer->tx_buf;
  uint32_t header;
  memcpy(&header, bytes, QSPI_HEADER_BYTES);
  QSPI_REG(QSPI_COMMAND_C_LOW) = 0;
  QSPI_REG(QSPI_COMMAND_C_HIGH) = header;
  QSPI_REG(QSPI_COMMAND_C_LINES) = QSPI_REG(QSPI_COMMAND_C_DATA) = 0;
  const size_t payload_length = xfer->len - QSPI_HEADER_BYTES;
  for (size_t offset = 0; offset < payload_length; offset += QSPI_WORD_BYTES) {
    uint32_t word = 0;
    memcpy(&word, bytes + QSPI_HEADER_BYTES + offset,
           payload_length - offset < QSPI_WORD_BYTES ? payload_length - offset : QSPI_WORD_BYTES);
    QSPI_REG(QSPI_COMMAND_FIFO + offset) = word;
  }
  const bool followed_by_quad_data =
      bus->state->idx + 1u < bus->state->num_xfers &&
      bus->state->xfers[bus->state->idx + 1u].tx_nbits == QSPI_QUAD_LANES;
  QSPI_REG(QSPI_COMMAND_C_LINES) = QSPI_HEADER_END << QSPI_HEADER_BYTE5_LINES_SHIFT;
  QSPI_REG(QSPI_COMMAND_C_DATA) =
      (payload_length << QSPI_COMMAND_LENGTH_SHIFT) |
      (followed_by_quad_data ? QSPI_DATA_QUAD << QSPI_COMMAND_DATA_LINES_SHIFT : 0u) |
      QSPI_COMMAND_START;
  s_command_status = -ETIMEDOUT;
  for (unsigned elapsed_us = 0; elapsed_us < QSPI_COMMAND_TIMEOUT_US; ++elapsed_us) {
    if (QSPI_REG(QSPI_STATUS) & QSPI_COMMAND_DONE) {
      QSPI_REG(QSPI_STATUS_CLEAR) = QSPI_COMMAND_DONE;
      QSPI_REG(QSPI_STATUS_CLEAR) = 0;
      s_command_status = 0;
      break;
    }
    delay_us(1);
  }
  QSPI_REG(QSPI_COMMAND_C_LINES) = QSPI_REG(QSPI_COMMAND_C_DATA) = 0;
  if (s_command_status == 0 && followed_by_quad_data) {
    QSPI_REG(QSPI_CONFIG) |= QSPI_STREAM_CONFIG;
    s_quad_stream_active = true;
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
