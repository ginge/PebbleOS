/* SPDX-License-Identifier: Apache-2.0 */
#include "task_memory.h"
#include "pbl/drivers/mpu.h"
#include <assert.h>
#include <stdio.h>

static void test_pebble_conversion(void) {
  MpuRegion source = {.enabled = true,
                      .executable = true,
                      .base_address = 0x20010000,
                      .size = 0x2000,
                      .cache_policy = MpuCachePolicy_WriteBackWriteAllocate};
  TuyaTaskMemory out;
  const MpuPermissions permissions[] = {MpuPermissions_PrivRW, MpuPermissions_PrivRW_UserRW,
                                        MpuPermissions_PrivRO, MpuPermissions_PrivRO_UserRO};
  const uint32_t expected[] = {TuyaMemory_ReadWrite | TuyaMemory_Privileged, TuyaMemory_ReadWrite,
                               TuyaMemory_ReadOnly | TuyaMemory_Privileged, TuyaMemory_ReadOnly};
  for (size_t i = 0; i < sizeof(permissions) / sizeof(*permissions); ++i) {
    source.permissions = permissions[i];
    assert(tuya_task_memory_from_pebble(&source, &out));
    assert(out.base == source.base_address && out.size == source.size);
    assert(out.flags == (expected[i] | TuyaMemory_Normal));
  }
  source.executable = false;
  assert(tuya_task_memory_from_pebble(&source, &out));
  assert(out.flags & TuyaMemory_ExecuteNever);
  source.permissions = MpuPermissions_PrivRW_UserRO;
  assert(!tuya_task_memory_from_pebble(&source, &out));
  assert(!out.base && !out.size && !out.flags);
  source.permissions = MpuPermissions_NoAccess;
  assert(!tuya_task_memory_from_pebble(&source, &out));
  source.permissions = MpuPermissions_PrivRW;
  for (int policy = MpuCachePolicy_NotCacheable; policy <= MpuCachePolicy_WriteBackNoWriteAllocate;
       ++policy) {
    source.cache_policy = policy;
    assert(tuya_task_memory_from_pebble(&source, &out) ==
           (policy == MpuCachePolicy_WriteBackWriteAllocate));
  }
  source.cache_policy = MpuCachePolicy_WriteBackWriteAllocate;
  source.size = 0x2001;
  assert(!tuya_task_memory_from_pebble(&source, &out));
  source.size = 0x2000;
  source.base_address = 0xfffff000u;
  assert(!tuya_task_memory_from_pebble(&source, &out));
#if UINTPTR_MAX > UINT32_MAX
  source.base_address = (uintptr_t)UINT32_MAX + 1u;
  assert(!tuya_task_memory_from_pebble(&source, &out));
#endif
  source.enabled = false;
  assert(tuya_task_memory_from_pebble(&source, &out));
  assert(!out.base && !out.size && !out.flags);
  assert(tuya_task_memory_from_pebble(NULL, &out));
  assert(!tuya_task_memory_from_pebble(&source, NULL));
}

int main(void) {
  test_pebble_conversion();
  const uint32_t rw = TuyaMemory_ReadWrite | TuyaMemory_Normal;
  TuyaTaskMemory regions[3] = {
      {0x20010000, 0x2000, rw}, {0x20020000, 0x2000, rw | TuyaMemory_Privileged}, {0}};
  assert(tuya_task_memory_validate(regions, 3, 0x20011000, 0x1000, false));
  assert(tuya_task_memory_validate(regions, 3, 0x20030000, 0x1000, true));
  assert(!tuya_task_memory_validate(regions, 3, 0x20021000, 0x1000, false));
  assert(tuya_task_memory_validate(regions, 3, 0x20021000, 0x1000, true));
  assert(!tuya_task_memory_validate(regions, 3, 0x20011000, 0x1020, false));
  assert(!tuya_task_memory_validate(regions, 3, 0x20011004, 0x1000, false));
  assert(!tuya_task_memory_validate(regions, 3, 0xffffffe0, 0x40, false));
  assert(!tuya_task_memory_validate(NULL, 3, 0x20011000, 0x1000, false));
  assert(!tuya_task_memory_validate(regions, 4, 0x20011000, 0x1000, false));
  regions[2] = (TuyaTaskMemory){0x20010000, 32, rw};
  assert(!tuya_task_memory_validate(regions, 3, 0x20011000, 0x1000, false));
  regions[2] = (TuyaTaskMemory){0};
  const uint32_t bad[] = {0,
                          TuyaMemory_Normal,
                          rw | TuyaMemory_ReadOnly,
                          rw | TuyaMemory_Device,
                          rw | (1u << 6),
                          TuyaMemory_ReadOnly | TuyaMemory_Normal};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
    regions[0].flags = bad[i];
    assert(!tuya_task_memory_validate(regions, 3, 0x20011000, 0x1000, false));
  }
  regions[0].flags = rw | TuyaMemory_ExecuteNever;
  assert(tuya_task_memory_validate(regions, 3, 0x20011000, 0x1000, false));
  regions[0].size = 0;
  assert(!tuya_task_memory_validate(regions, 3, 0x20011000, 0x1000, false));
  puts("Task memory: alignment, bounds, permissions and overlap checks passed");
}
