/* SPDX-License-Identifier: Apache-2.0 */
#include "svc_bridge.h"
#include "pbl/mcu/syscall_abi.h"
#include <string.h>

_Static_assert(PBL_SYSCALL_SVC > 3 && PBL_SYSCALL_SVC <= 255, "Tuya SVC ABI collision");

enum { BASIC_WORDS = 8, FP_WORDS = 18, ARG_WORDS = 16, LR_WORD = 5, PC_WORD = 6, PSR_WORD = 7 };

static bool prv_contains(TuyaSvcRange range, uintptr_t address, size_t size) {
  return range.high > range.low && address >= range.low && address <= range.high &&
         size <= range.high - address;
}

TuyaSvcResult tuya_svc_dispatch(uint32_t *frame, uint32_t exc_return, uint32_t control,
                                uint32_t core, uint32_t pebble_core, const TuyaSvcHooks *hooks) {
  /* Privileged vendor startup still owns its original SVC ABI. */
  if (!(control & 1u)) {
    return TuyaSvc_Vendor;
  }
  if (core != pebble_core || !hooks || !hooks->task_stack || !hooks->authorize ||
      !hooks->instruction || !hooks->syscall_stack || !hooks->materialize_fp ||
      !hooks->setup_return || !hooks->commit) {
    return TuyaSvc_Denied;
  }
  if (exc_return != 0xfffffffdu && exc_return != 0xffffffedu && exc_return != 0xffffffbcu &&
      exc_return != 0xffffffacu) {
    return TuyaSvc_Denied;
  }
  TuyaSvcRange source;
  const uintptr_t sp = (uintptr_t)frame;
  if (!sp || (sp & 3u) || !hooks->task_stack(hooks->context, &source) ||
      !prv_contains(source, sp, BASIC_WORDS * sizeof(uint32_t))) {
    return TuyaSvc_Denied;
  }
  const uint32_t psr = frame[PSR_WORD];
  const bool fp = !(exc_return & 0x10u);
  const size_t words = BASIC_WORDS + (fp ? FP_WORDS : 0);
  const size_t bytes = (words + ((psr & (1u << 9)) ? 1u : 0u)) * sizeof(uint32_t);
  if (!(psr & (1u << 24)) || (psr & 0x1ffu) || !prv_contains(source, sp, bytes)) {
    return TuyaSvc_Denied;
  }
  const uint32_t pc = frame[PC_WORD];
  if (pc < 2 || (pc & 1u) || !hooks->authorize(hooks->context, pc) ||
      hooks->instruction(hooks->context, pc - 2) != (0xdf00u | PBL_SYSCALL_SVC)) {
    return TuyaSvc_Denied;
  }
  const uintptr_t original_sp = sp + bytes;
  TuyaSvcRange target;
  if (!hooks->syscall_stack(hooks->context, &target)) {
    return TuyaSvc_Denied;
  }
  uint32_t *destination = frame;
  const bool relocate = target.low || target.high;
  if (relocate) {
    const size_t needed = (words + ARG_WORDS) * sizeof(uint32_t);
    if (!target.low || (target.low & 7u) || (target.high & 7u) || target.high <= target.low ||
        target.high - target.low < needed ||
        !(target.high <= source.low || target.low >= source.high)) {
      return TuyaSvc_Denied;
    }
    destination = (uint32_t *)(target.high - needed);
  }
  /* Force lazy FP stacking while PSP still refers to the original frame. */
  if (fp) {
    hooks->materialize_fp(hooks->context);
  }
  if (relocate) {
    const size_t available_words = (source.high - original_sp) / sizeof(uint32_t);
    const size_t copy_words = available_words < ARG_WORDS ? available_words : ARG_WORDS;
    memcpy(destination, frame, words * sizeof(uint32_t));
    uint32_t *args = destination + words;
    memset(args, 0, ARG_WORDS * sizeof(uint32_t));
    memcpy(args, (const void *)original_sp, copy_words * sizeof(uint32_t));
    destination[PSR_WORD] &= ~(1u << 9);
  }
  hooks->setup_return(hooks->context, original_sp, destination + LR_WORD);
  hooks->commit(hooks->context, destination, relocate ? target.low : 0);
  return TuyaSvc_Entered;
}
