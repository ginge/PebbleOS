/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <cmsis_core.h>

uint32_t SystemCoreClock = 120000000u;

extern uint8_t __ISR_VECTOR_TABLE__[];

/* The vendor boot stages clocks, flash mapping and the application CPU. */
void SystemInit(void) {
  __disable_irq();

  /* The entire T5AI image is built with -mfpu=fpv5-sp-d16, so compiler-
   * generated FP instructions are possible before the scheduler starts.
   * Match the pinned BK7258 CMSIS SystemInit and grant full access to CP10 and
   * CP11 here. The FreeRTOS port repeats this when it enables lazy FP context
   * preservation, but that is too late for pre-scheduler C code. */
  SCB->CPACR |= (3u << 20u) | (3u << 22u);

  SCB->VTOR = (uintptr_t)__ISR_VECTOR_TABLE__;
  __DSB();
  __ISB();
}

void soc_early_init(void) {
  __enable_irq();
}
