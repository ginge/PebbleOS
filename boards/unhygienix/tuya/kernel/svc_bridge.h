/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Half-open ranges. Both zero means no dedicated syscall stack. */
typedef struct {
  uintptr_t low;
  uintptr_t high;
} TuyaSvcRange;

typedef struct {
  void *context;
  bool (*task_stack)(void *context, TuyaSvcRange *range);
  bool (*authorize)(void *context, uint32_t return_pc);
  uint16_t (*instruction)(void *context, uint32_t address);
  bool (*syscall_stack)(void *context, TuyaSvcRange *range);
  void (*materialize_fp)(void *context);
  void (*setup_return)(void *context, uintptr_t original_sp, uint32_t *lr);
  void (*commit)(void *context, uint32_t *frame, uintptr_t stack_limit);
} TuyaSvcHooks;

typedef enum {
  TuyaSvc_Vendor,
  TuyaSvc_Denied,
  TuyaSvc_Entered,
} TuyaSvcResult;

/* Runs with exceptions excluding task switches. Hooks are privileged code;
 * untrusted stack data is bounds-checked before any access or modification.
 * Secure-only and non-secure-only basic/FP thread frames are supported, not
 * TrustZone-crossing frames. Caller must check its configured security state. */
TuyaSvcResult tuya_svc_dispatch(uint32_t *frame, uint32_t exc_return, uint32_t control,
                                uint32_t core, uint32_t pebble_core, const TuyaSvcHooks *hooks);
