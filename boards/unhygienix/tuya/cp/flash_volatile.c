/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
/* GD25WQ64E (JEDEC C8 65 17), datasheet rev 1.2, sections 7.4/7.5.
 * Use the pinned RAM-resident SPI bypass to issue 50h immediately followed
 * by 01h/SR1, without the controller's automatic nonvolatile WREN sequence.
 * Only BP changes during a server write qualify. All other cases stay stock.
 * EXPERIMENTAL: hardware readback failed on 2026-10-11; keep disabled.
 * See docs/boards/unhygienix/flash_optimisation.md for preserved failure evidence.
 */
#ifdef PEBBLE_FLASH_VOLATILE_STATUS
#include <stdbool.h>
#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define NOW      REG32(0x4400020c)
extern void bk_printf(const char *, ...);
extern uint32_t pebble_flash_wait(uint32_t);
extern uint32_t pebble_flash_launch(uint32_t);
extern void stock_status(uint32_t);
extern int stock_bypass_write(uint8_t *, uint8_t *, uint32_t);
extern int pebble_flash_prepare(void);
extern int pebble_flash_finish(void);
extern uint32_t pebble_flash_irq_enter(void);
extern void pebble_flash_irq_exit(uint32_t);
extern void pebble_flash_fatal(void) __attribute__((noreturn));

static uint32_t read_status(uint32_t base) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 2; ++i) {
    pebble_flash_wait(base);
    REG32(base + 28) = 0;
    REG32(base + 84) = (REG32(base + 84) & ~(31u << 24)) | ((i ? 6u : 3u) << 24);
    pebble_flash_launch(base); /* Starts and waits entirely in ITCM. */
    value |= (REG32(base + 36) & 255u) << (i * 8);
  }
  return value;
}
static void fail(unsigned stage, uint32_t actual, uint32_t requested, uint32_t flags) {
  pebble_flash_irq_exit(flags); /* SPI routing is restored at every caller. */
  bk_printf("T5VOL fault=%u actual=%lx requested=%lx\r\n", stage, (unsigned long)actual,
            (unsigned long)requested);
  pebble_flash_fatal();
}
static uint32_t ready_status(uint32_t base, uint32_t start, uint32_t flags) {
  for (;;) {
    uint32_t actual = read_status(base);
    if (!(actual & 1u))
      return actual;
    if ((uint32_t)(NOW - start) >= 3200u)
      fail(1, actual, 0, flags);
  }
}
bool pebble_flash_volatile_status(uint32_t old, uint32_t requested) {
  /* JEDEC from the verified s_flash layout, not the programmer's byte order. */
  if (REG32(0x2806a734) != 0xc86517u || ((old ^ requested) & ~0x7cu))
    return false;
  uint32_t base = REG32(0x2806a72c);
  pebble_flash_prepare(); /* Notification ACK only; this does NOT park AP XIP. */
  uint32_t flags = pebble_flash_irq_enter(), start = NOW;
  uint32_t actual = ready_status(base, start, flags);
  if (((actual ^ old) & 0xfffcu) || (actual & 2u))
    fail(2, actual, old, flags);
  /* Experimental SPI re-protection failed readback. Keep the stock restore command,
   * whose baseline cost was only 0.168 ms/write, and verify it physically too. */
  int spi_result = 0;
  bool fallback = false;
  if (requested & 0x7cu) {
    stock_status(requested);
  } else {
    uint8_t enable = 0x50, payload[2] = {0x01, (uint8_t)requested & 0xfcu};
    spi_result = stock_bypass_write(&enable, payload, sizeof(payload));
  }
  /* Allow settling before readback; no command intervenes between 50h and 01h. */
  uint32_t settle = NOW;
  while ((uint32_t)(NOW - settle) < 2u) {
  }
  actual = ready_status(base, start, flags);
  if (((actual ^ requested) & 0xfffcu) || (actual & 2u)) {
    /* A completed SPI frame is not proof the NOR accepted it. Recover only
     * from an unchanged, idle, protected status using the stock controller.
     * Never retry a SPI command or proceed on a partial/unexpected change. */
    if ((requested & 0x7cu) || ((actual ^ old) & 0xfffcu) || (actual & 3u))
      fail(4, actual, requested, flags);
    fallback = true;
    stock_status(requested);
    actual = ready_status(base, NOW, flags);
    if (((actual ^ requested) & 0xfffcu) || (actual & 2u))
      fail(5, actual, requested, flags);
  }
  REG32(0x2806a738) = actual; /* Publish only verified status to stock protection logic. */
  pebble_flash_irq_exit(flags);
  pebble_flash_finish();
  if (fallback || spi_result)
    bk_printf("T5SPI recovered fallback=%u spi_ret=%d status=%lx\r\n", (unsigned)fallback,
              spi_result, (unsigned long)actual);
  return true;
}
#endif
