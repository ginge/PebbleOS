/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
/* Experimental pinned bypass ABI. CP transfer code is RAM-resident.
 * This does not park the AP: packaging is blocked until AP ownership is solved. */
#ifdef PEBBLE_FLASH_VOLATILE_STATUS
#include <stdint.h>
#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SPI  0x44870000u
#define SYS  0x44010000u
#define AON  R(0x4400020c)
#define RAM  __attribute__((section(".spi_guard"), noinline))
extern uint32_t stock_irq_disable(void);
extern void stock_irq_enable(uint32_t);
static RAM int transfer(const uint8_t *data, uint32_t size, uint32_t start) {
  R(SPI + 20) = 0;
  R(SPI + 24) = 0x37f00; /* Clear FIFOs and W1C events, not ready bits. */
  /* FIFO clear crosses the peripheral clock domain; do not preload until it
   * has settled. Ready may retain its pre-clear value during that crossing. */
  uint32_t clear = AON;
  while ((uint32_t)(AON - clear) < 2u) {
  }
  uint32_t tx = 0;
  /* FIFO has room before TX_EN: no clock can precede the command byte. */
  while (tx < size && tx < 16 && (R(SPI + 24) & 2u))
    R(SPI + 28) = data[tx++];
  if (!tx)
    return -2;
  R(SPI + 20) = (size << 8) | 5u;
  for (;;) {
    uint32_t state = R(SPI + 24);
    if (state & 0x1800u)
      return -3; /* Underflow or overflow is a failed transfer. */
    if (tx < size && (state & 2u))
      R(SPI + 28) = data[tx++];
    if (state & 4u) {
      (void)R(SPI + 28);
    }
    if (tx == size && (state & 0x2000u))
      break;
    if ((uint32_t)(AON - start) >= 3200u)
      return -1;
  }
  /* FIFO completion is not a pin-idle guarantee. Allow the final SCLK edge
   * and device CS hold time before changing TX_EN or the routing mux. */
  uint32_t hold = AON;
  while ((uint32_t)(AON - hold) < 2u) {
  }
  R(SPI + 20) = 0; /* Explicit CS release; TX_EN must be zero. */
  uint32_t end = AON;
  while ((uint32_t)(AON - end) < 1u) {
  } /* >= one AON tick between commands. */
  return 0;
}
RAM int pebble_spi_transfer(uint8_t *prefix, uint8_t *data, uint32_t size) {
  uint32_t route = R(SYS + 8), start = AON;
  R(SYS + 8) = route | 512u;
  __asm__ volatile("dsb; isb" ::: "memory");
  uint32_t settle = AON;
  while ((uint32_t)(AON - settle) < 2u) {
  } /* Route and idle levels settle before CS. */
  int result = prefix ? transfer(prefix, 1, start) : 0;
  if (!result && size)
    result = transfer(data, size, start);
  R(SPI + 20) = 0;
  R(SYS + 8) = route;
  __asm__ volatile("dsb; isb" ::: "memory");
  return result;
}
int pebble_spi_bypass_write(uint8_t *prefix, uint8_t *data, uint32_t size) {
  if (size > 4095u || (size && !data))
    return -4;
  uint32_t flags = stock_irq_disable();
  uint32_t ctrl = R(SPI + 16), cfg = R(SPI + 20), reset = R(SPI + 8);
  uint32_t clock = R(SYS + 48), select = R(SYS + 40);
  if (cfg & 3u) {
    stock_irq_enable(flags);
    return -5;
  }
  R(SYS + 48) = clock & ~2u;
  R(SPI + 16) = 0;
  R(SPI + 20) = 0;
  R(SYS + 48) = clock | 2u;
  R(SYS + 40) = select & ~16u;
  R(SPI + 8) = reset | 3u;
  /* Vendor CONFIG_SYSTEM_CTRL HAL implies 26 MHz / (2 * 7), about 1.86 MHz.
   * Wire frequency is unmeasured. Mode 0, byte interval and RX sample edge 1. */
  R(SPI + 16) = 0x41c00700u;
  R(SPI + 20) = 0;
  uint32_t start = AON;
  while ((uint32_t)(AON - start) < 2u) {
  }
  int result = pebble_spi_transfer(prefix, data, size);
  /* DATA is a FIFO port, never a restorable configuration register. */
  R(SPI + 24) = 0x37f00;
  R(SPI + 16) = 0;
  R(SPI + 16) = ctrl;
  R(SPI + 20) = cfg;
  R(SPI + 8) = reset;
  R(SYS + 40) = select;
  R(SYS + 48) = clock;
  stock_irq_enable(flags);
  return result;
}
#endif
