/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/mcu/cache.h>

#include <cmsis_core.h>

// The boot stages may leave STAR-MC1 D-cache enabled. Preserve that policy;
// DMA still needs real maintenance even though generic ARM D-cache is disabled.
#define CACHE_LINE_SIZE 32u
static void prv_range(volatile uint32_t *op, const void *address, size_t size) {
  if (size == 0 || !(SCB->CCR & SCB_CCR_DC_Msk))
    return;
  const uintptr_t first = (uintptr_t)address & ~(uintptr_t)(CACHE_LINE_SIZE - 1u);
  const uintptr_t end =
      ((uintptr_t)address + size + CACHE_LINE_SIZE - 1u) & ~(uintptr_t)(CACHE_LINE_SIZE - 1u);
  __DSB();
  for (uintptr_t line = first; line < end; line += CACHE_LINE_SIZE) {
    *op = line;
  }
  __DSB();
  __ISB();
}
void dcache_flush(const void *address, size_t size) {
  prv_range(&SCB->DCCMVAC, address, size);
}
void dcache_invalidate(void *address, size_t size) {
  prv_range(&SCB->DCIMVAC, address, size);
}
void dcache_flush_invalidate(const void *address, size_t size) {
  prv_range(&SCB->DCCIMVAC, address, size);
}
