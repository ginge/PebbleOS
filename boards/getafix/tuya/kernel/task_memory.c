/* SPDX-License-Identifier: Apache-2.0 */
#include "task_memory.h"

static bool prv_range(uint32_t base, uint32_t size) {
  return base && size && !(base & 31u) && !(size & 31u) && size <= UINT32_MAX - base;
}
static bool prv_overlap(uint32_t a, uint32_t as, uint32_t b, uint32_t bs) {
  return a < b + bs && b < a + as;
}
bool tuya_task_memory_validate(const TuyaTaskMemory *regions, size_t count, uint32_t stack_base,
                               uint32_t stack_size, bool privileged) {
  if (!regions || count != 3 || !prv_range(stack_base, stack_size)) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    const TuyaTaskMemory *r = &regions[i];
    if (!r->size) {
      if (r->base || r->flags) {
        return false;
      }
      continue;
    }
    const uint32_t access = r->flags & (TuyaMemory_ReadOnly | TuyaMemory_ReadWrite);
    if (!prv_range(r->base, r->size) ||
        (r->flags & ~(TuyaMemory_ReadOnly | TuyaMemory_ReadWrite | TuyaMemory_ExecuteNever |
                      TuyaMemory_Normal | TuyaMemory_Privileged)) ||
        !(r->flags & TuyaMemory_Normal) ||
        (access != TuyaMemory_ReadOnly && access != TuyaMemory_ReadWrite)) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (regions[j].size && prv_overlap(r->base, r->size, regions[j].base, regions[j].size)) {
        return false;
      }
    }
    if (prv_overlap(r->base, r->size, stack_base, stack_size) &&
        (stack_base < r->base || stack_base + stack_size > r->base + r->size ||
         access != TuyaMemory_ReadWrite || (!privileged && (r->flags & TuyaMemory_Privileged)))) {
      return false;
    }
  }
  return true;
}
