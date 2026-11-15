/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Disable all old ARMv8-M task regions before changing bases.
#define T5AI_RESTORE_MPU \
  "  mrs r12, primask \n"                                                   \
  "  cpsid i \n"                                                            \
  "  dsb \n"                                                                \
  "  ldr r2, =0xe000ed98 \n" /* MPU_RNR */                                    \
  "  mov r4, #" STR(FIRST_MPU_REGION) " \n"                                  \
  "  str r4, [r2] \n"                                                        \
  "  ldr r2, =0xe000ed9c \n" /* MPU_RBAR and its aliases */                   \
  "  ldmia r1!, {r4-r11} \n"                                                 \
  "  mov r3, #0 \n"                                                          \
  "  str r3, [r2, #4] \n"  /* MPU_RLAR */                                   \
  "  str r3, [r2, #12] \n" /* MPU_RLAR_A1 */                                \
  "  str r3, [r2, #20] \n" /* MPU_RLAR_A2 */                                \
  "  str r3, [r2, #28] \n" /* MPU_RLAR_A3 */                                \
  "  dsb \n"                                                                \
  "  isb \n"                                                                \
  "  stmia r2!, {r4-r11} \n"                                                 \
  "  dsb \n"                                                                \
  "  isb \n"                                                                \
  "  msr primask, r12 \n"
