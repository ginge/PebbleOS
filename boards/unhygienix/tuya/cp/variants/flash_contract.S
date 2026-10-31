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

