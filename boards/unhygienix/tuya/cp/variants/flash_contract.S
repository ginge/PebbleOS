/* SPDX-License-Identifier: Apache-2.0 */
.syntax unified
.thumb
.section .itcm_sec_code.unhygienix_flash,"ax",%progbits
.global unhygienix_cp_flash_wait
.type unhygienix_cp_flash_wait,%function
.thumb_func
unhygienix_cp_flash_wait:
  push {r1-r4, lr}
  ldr r1, =0x4400020c
  ldr r2, [r1]
  ldr r4, =160000  /* Five seconds on the independent 32 kHz AON clock. */
1:
  ldr r3, [r0, #16]
  cmp r3, #0
  bpl 2f
  ldr r3, [r1]
  subs r3, r3, r2
  cmp r3, r4
  blo 1b
  b unhygienix_cp_flash_fatal
2:
  mov r0, r3
  pop {r1-r4, pc}
.size unhygienix_cp_flash_wait, .-unhygienix_cp_flash_wait
.global unhygienix_cp_flash_fatal
.type unhygienix_cp_flash_fatal,%function
.thumb_func
unhygienix_cp_flash_fatal:
  cpsid i
  ldr r0, =0x44000000
  ldr r1, [r0]
  bic r1, r1, #0x7f000000
  orr r1, r1, #0x7e000000  /* Distinct AP-visible CP flash guard cause. */
  str r1, [r0]
  ldr r1, [r0, #0x104]
  bic r1, r1, #3
  str r1, [r0, #0x104]
  ldr r1, [r0, #8]
  bic r1, r1, #63
  orr r1, r1, #0x26
  str r1, [r0, #8]
  ldr r0, =0x44800000
  ldr r1, [r0, #8]
  orr r1, r1, #2
  str r1, [r0, #8]
  ldr r1, =0x005a000a
  ldr r2, =0x00a5000a
  str r1, [r0, #16]
  str r2, [r0, #16]
  ldr r0, =0x44000600
  str r1, [r0]
  str r2, [r0]
  dsb
3:
  wfi
  b 3b
.ltorg

/* Launch and wait in RAM, including when the controller blocks XIP fetches. */
.global unhygienix_cp_flash_launch
.type unhygienix_cp_flash_launch,%function
.thumb_func
unhygienix_cp_flash_launch:
  ldr r1, [r0, #16]
  orr r1, r1, #0x20000000
  str r1, [r0, #16]
  b unhygienix_cp_flash_wait


/* r0: CP control register, r1: preserved control with only the IRQ mask added.
 * SCR and every guard are prepared before this leaf. No data/stack access may
 * follow the mask store until WFI has returned. */
.section .itcm_sec_code.unhygienix_sleep,"ax",%progbits
.global unhygienix_cp_masked_wfi
.type unhygienix_cp_masked_wfi,%function
.thumb_func
unhygienix_cp_masked_wfi:
  str r1, [r0]
  dsb sy
  wfi
  isb sy
  bx lr
.size unhygienix_cp_masked_wfi, .-unhygienix_cp_masked_wfi

/* Test-only return breadcrumb. r2 contains only the owned CP reason bits.
 * Reload MMIO addresses after WFI and preserve every other owner's live field.
 * All literal loads use ITCM; no stack/SRAM access follows the mask store. */
.equ UNHYGIENIX_AON_PMU_REASON, 0x44000000
.equ UNHYGIENIX_AON_PMU_REASON_COMMIT, 0x44000094
.equ UNHYGIENIX_CP_REASON_SHIFT, 4
.equ UNHYGIENIX_CP_REASON_MASK, (0xff << UNHYGIENIX_CP_REASON_SHIFT)
.equ UNHYGIENIX_REASON_COMMIT_KEY_1, 0x424b55aa
.equ UNHYGIENIX_REASON_COMMIT_KEY_2, 0xbdb4aa55
.balign 4
.global unhygienix_cp_masked_wfi_trace
.type unhygienix_cp_masked_wfi_trace,%function
.thumb_func
unhygienix_cp_masked_wfi_trace:
  str r1, [r0]
  dsb sy
  wfi
  isb sy
  and r0, r2, #UNHYGIENIX_CP_REASON_MASK
  ldr r3, =UNHYGIENIX_AON_PMU_REASON
  ldr r2, [r3]
  bic r2, r2, #UNHYGIENIX_CP_REASON_MASK
  orr r2, r2, r0
  str r2, [r3]
  ldr ip, =UNHYGIENIX_AON_PMU_REASON_COMMIT
  ldr r1, =UNHYGIENIX_REASON_COMMIT_KEY_1
  str r1, [ip]
  ldr r1, =UNHYGIENIX_REASON_COMMIT_KEY_2
  str r1, [ip]
  dsb sy
  bx lr
  .ltorg
.size unhygienix_cp_masked_wfi_trace, .-unhygienix_cp_masked_wfi_trace

/* Diagnostic mask-store/barrier isolation. r2 preserves the original word.
 * No WFI, calls, stack or data accesses other than the owned control register. */
.balign 4
.global unhygienix_cp_mask_barrier
.type unhygienix_cp_mask_barrier,%function
.thumb_func
unhygienix_cp_mask_barrier:
  str r1, [r0]
  dsb sy
  str r2, [r0]
  dsb sy
  isb sy
  bx lr
.size unhygienix_cp_mask_barrier, .-unhygienix_cp_mask_barrier
