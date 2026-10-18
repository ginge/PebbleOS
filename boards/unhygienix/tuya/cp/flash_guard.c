/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define NOW      REG32(0x4400020c)
enum {
  WRITE,
  UNPROTECT,
  REPROTECT,
  STATUS,
  MODE,
  START,
  END,
  PROGRAM,
  IRQ,
  PHASES
};
typedef struct {
  uint32_t count, total, max;
} Timing;
typedef struct {
  Timing phase[PHASES];
  uint32_t bytes, old_status, new_status, changes, irq_start, irq_depth, failures, active,
      program_start;
} FlashTrace;
_Static_assert(sizeof(FlashTrace) == 144, "CP IRAM trace reservation changed");
#define TRACE ((volatile FlashTrace *)(uintptr_t)0x28067360)
extern int stock_flash_write(uint32_t, const uint8_t *, uint32_t);
extern void stock_protect(unsigned);
extern void stock_status(uint32_t);
#ifdef PEBBLE_FLASH_VOLATILE_STATUS
#include <stdbool.h>
extern bool pebble_flash_volatile_status(uint32_t, uint32_t);
#endif
extern unsigned stock_mode(unsigned);
extern int stock_prepare(void);
extern int stock_finish(void);
extern int stock_cpu_off(unsigned);
extern uint32_t stock_irq_disable(void);
extern void stock_irq_enable(uint32_t);
extern void bk_printf(const char *, ...);
extern void pebble_flash_fatal(void) __attribute__((noreturn));

static void sample(unsigned phase, uint32_t start) {
  if (!TRACE->active)
    return;
  uint32_t elapsed = NOW - start;
  volatile Timing *t = &TRACE->phase[phase];
  ++t->count;
  t->total += elapsed;
  if (elapsed > t->max)
    t->max = elapsed;
}
void pebble_flash_protect(unsigned type) {
  uint32_t start = NOW;
  stock_protect(type);
  sample(type == 0 ? UNPROTECT : REPROTECT, start);
}
void pebble_flash_status(uint32_t status) {
  uint32_t old = REG32(0x2806a738), start = NOW;
#ifdef PEBBLE_FLASH_VOLATILE_STATUS
  if (TRACE->active && TRACE->phase[STATUS].count == 0)
    bk_printf("T5VOL first old=%lx requested=%lx id=%lx base=%lx\r\n", (unsigned long)old,
              (unsigned long)status, (unsigned long)REG32(0x2806a734),
              (unsigned long)REG32(0x2806a72c));
  if (!TRACE->active || !pebble_flash_volatile_status(old, status))
#endif
    stock_status(status);
  if (TRACE->active) {
    TRACE->old_status = old;
    TRACE->new_status = status;
    if (old != status)
      ++TRACE->changes;
  }
  sample(STATUS, start);
}
unsigned pebble_flash_mode(unsigned mode) {
  uint32_t start = NOW;
  unsigned result = stock_mode(mode);
  sample(MODE, start);
  return result;
}
static int notify(int (*call)(void), unsigned phase) {
  int powered_off = stock_cpu_off(1);
  uint32_t start = NOW;
  int result = call();
  sample(phase, start);
  // The stock iteration cap can also return success without an ACK.
  if (result != 0 || (!powered_off && REG32(0x2806b36c) != 2)) {
    ++TRACE->failures;
    pebble_flash_fatal();
  }
  return result;
}
int pebble_flash_prepare(void) {
  return notify(stock_prepare, START);
}
int pebble_flash_finish(void) {
  return notify(stock_finish, END);
}
uint32_t pebble_flash_irq_enter(void) {
  uint32_t flags = stock_irq_disable();
  if (TRACE->irq_depth++ == 0)
    TRACE->irq_start = NOW;
  return flags;
}
void pebble_flash_irq_exit(uint32_t flags) {
  if (--TRACE->irq_depth == 0)
    sample(IRQ, TRACE->irq_start);
  stock_irq_enable(flags);
}
uint32_t pebble_flash_program_enter(void) {
  uint32_t flags = pebble_flash_irq_enter();
  TRACE->program_start = NOW;
  return flags;
}
void pebble_flash_program_exit(uint32_t flags) {
  sample(PROGRAM, TRACE->program_start);
  pebble_flash_irq_exit(flags);
}
int pebble_flash_write(uint32_t address, const uint8_t *buffer, uint32_t length) {
  // Called by the stock server before its protected write API. No new ABI.
  TRACE->active = 1;
  uint32_t start = NOW;
  int result = stock_flash_write(address, buffer, length);
  sample(WRITE, start);
  TRACE->bytes += length;
  TRACE->active = 0;
  // Buffered counters only; report after scheduler/IRQs and protection restore.
  if ((TRACE->phase[WRITE].count & 127u) == 0) {
    bk_printf(
        "T5FLASH count=%lu bytes=%lu status_changes=%lu old=%lx new=%lx irq_max_aon=%lu"
#ifdef PEBBLE_FLASH_VOLATILE_STATUS
        " volatile=1"
#endif
        "\r\n",
        (unsigned long)TRACE->phase[WRITE].count, (unsigned long)TRACE->bytes,
        (unsigned long)TRACE->changes, (unsigned long)TRACE->old_status,
        (unsigned long)TRACE->new_status, (unsigned long)TRACE->phase[IRQ].max);
    for (unsigned i = 0; i < PHASES; ++i) {
      bk_printf("T5FLASH phase=%u count=%lu total_aon=%lu max_aon=%lu\r\n", i,
                (unsigned long)TRACE->phase[i].count, (unsigned long)TRACE->phase[i].total,
                (unsigned long)TRACE->phase[i].max);
    }
  }
  return result;
}
