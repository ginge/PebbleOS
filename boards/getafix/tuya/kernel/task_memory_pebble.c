/* SPDX-License-Identifier: Apache-2.0 */
#include "task_memory.h"
#include "pbl/drivers/mpu.h"

bool tuya_task_memory_from_pebble(const MpuRegion *region, TuyaTaskMemory *out) {
  if (!out) {
    return false;
  }
  *out = (TuyaTaskMemory){0};
  if (!region || !region->enabled) {
    return true;
  }
  if (!region->base_address || region->base_address > UINT32_MAX || !region->size ||
      (region->base_address & 31u) || (region->size & 31u) ||
      region->size > UINT32_MAX - region->base_address ||
      region->cache_policy != MpuCachePolicy_WriteBackWriteAllocate) {
    return false;
  }
  uint32_t flags = TuyaMemory_Normal;
  switch (region->permissions) {
    case MpuPermissions_PrivRW:
      flags |= TuyaMemory_ReadWrite | TuyaMemory_Privileged;
      break;
    case MpuPermissions_PrivRW_UserRW:
      flags |= TuyaMemory_ReadWrite;
      break;
    case MpuPermissions_PrivRO:
      flags |= TuyaMemory_ReadOnly | TuyaMemory_Privileged;
      break;
    case MpuPermissions_PrivRO_UserRO:
      flags |= TuyaMemory_ReadOnly;
      break;
    default:
      return false;
  }
  if (!region->executable) {
    flags |= TuyaMemory_ExecuteNever;
  }
  *out = (TuyaTaskMemory){(uint32_t)region->base_address, region->size, flags};
  return true;
}
