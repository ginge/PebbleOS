/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Matches the pinned vendor descriptor flags, with one explicit extension. */
enum {
  TuyaMemory_ReadOnly = 1u << 0,
  TuyaMemory_ReadWrite = 1u << 1,
  TuyaMemory_ExecuteNever = 1u << 2,
  TuyaMemory_Normal = 1u << 3,
  TuyaMemory_Device = 1u << 4,
  TuyaMemory_Privileged = 1u << 5,
};
typedef struct {
  uint32_t base;
  uint32_t size;
  uint32_t flags;
} TuyaTaskMemory;

/* Reject rounding, overflow, unsupported flags and overlapping MPU regions.
 * The stack may be wholly inside one region; partial overlap is invalid. */
bool tuya_task_memory_validate(const TuyaTaskMemory *regions, size_t count, uint32_t stack_base,
                               uint32_t stack_size, bool privileged);

struct MpuRegion;
/* Converts logical Pebble policy, never reinterprets encoded RBAR/RLAR words. */
bool tuya_task_memory_from_pebble(const struct MpuRegion *region, TuyaTaskMemory *out);
