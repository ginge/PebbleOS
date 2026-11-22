/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/kernel/cache.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/system/rtc_registers.h>

#include RETAINED_SOURCE
#include "pebble/system/bootbits.c"
#include "pebble/system/reboot_reason.c"

static unsigned mask, point, cut_at, warnings, will_set, did_set;
static bool injecting;
static unsigned char durable[sizeof(s_retained)];
static jmp_buf lost_power;
static void test_point(void) {
  if (injecting && ++point == cut_at)
    longjmp(lost_power, 1);
}
uint32_t test_mask_get(void) {
  return mask;
}
void test_mask_set(uint32_t value) {
  mask = value;
}
void test_barrier(void) {
  test_point();
}
void arch_dcache_flush_range(uintptr_t addr, size_t size) {
  const uintptr_t base = (uintptr_t)&s_retained;
  assert(addr >= base && size <= sizeof(s_retained) && addr - base + size <= sizeof(s_retained));
  // Model publication a word at a time, including interrupted cache publication.
  for (size_t i = 0; i < size; i += sizeof(uint32_t)) {
    const size_t n = size - i < sizeof(uint32_t) ? size - i : sizeof(uint32_t);
    memcpy(durable + (addr - base) + i, (const unsigned char *)addr + i, n);
    test_point();
  }
}
bool pbl_irq_is_locked(void) {
  return mask != 0;
}
bool pbl_kernel_is_running(void) {
  return true;
}
void pbl_log(uint8_t level, const char *file, int line, const char *fmt, ...) {
  if (level == LOG_LEVEL_WARNING)
    ++warnings;
}
void reboot_reason_platform_will_set(const RebootReason *reason) {
  ++will_set;
}
void reboot_reason_platform_did_set(const RebootReason *reason) {
  ++did_set;
}
void reboot_reason_platform_get(RebootReason *reason) {
}
void reboot_reason_platform_will_clear(void) {
}
void reboot_reason_platform_did_clear(void) {
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  abort();
}

static void load_image(const unsigned char *image) {
  memcpy((void *)&s_retained, image, sizeof(s_retained));
  memcpy(durable, image, sizeof(durable));
  mask = point = cut_at = 0;
  injecting = false;
}
static void seed_image(void) {
  t5ai_retained_init();
  for (unsigned i = 0; i < T5AI_RETAINED_WORDS; ++i)
    retained_write(i, 0x10203040u + i);
  retained_write(T5AI_RTC_EXTERNAL_UNTRUSTED, T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE);
  t5ai_retained_write_clock(1800000000u, UINT64_C(0x100001000));
}
