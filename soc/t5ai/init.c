/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <cmsis_core.h>

uint32_t SystemCoreClock = 120000000u;

extern uint8_t __ISR_VECTOR_TABLE__[];

/* The vendor boot stages clocks, flash mapping and the application CPU. */
void SystemInit(void) {
  __disable_irq();
  SCB->VTOR = (uintptr_t)__ISR_VECTOR_TABLE__;
  __DSB();
  __ISB();
}

void soc_early_init(void) {
  __enable_irq();
}
