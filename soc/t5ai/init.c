/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/reset.h>
#include <cmsis_core.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include "pbl/mcu/cache.h"

uint32_t SystemCoreClock = CONFIG_T5AI_CORE_CLOCK_HZ;

extern uint8_t arch_vector_table[];

/* The vendor boot stages clocks, flash mapping and the application CPU. */
void SystemInit(void) {
  __disable_irq();

  /* The entire T5AI image is built with -mfpu=fpv5-sp-d16, so compiler-
   * generated FP instructions are possible before the scheduler starts.
   * Match the pinned BK7258 CMSIS SystemInit and grant full access to CP10 and
   * CP11 here before entering the native kernel and its lazy FP context setup. */
  SCB->CPACR |= (3u << 20u) | (3u << 22u);

  /* Do not inherit scheduler state from the boot stages. pbl_soc_early_init()
   * enables interrupts before the native scheduler starts, so an enabled or pending
   * SysTick/PendSV here would vector into handlers that are not ready yet. */
  SysTick->CTRL = 0u;
  SysTick->LOAD = 0u;
  SysTick->VAL = 0u;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

  /* Pebble and the ARM EABI require 8-byte stack alignment at exception
   * entry. Make this explicit rather than depending on the bootloader's CCR. */
  SCB->CCR |= SCB_CCR_STKALIGN_Msk;

  SCB->VTOR = (uintptr_t)arch_vector_table;
  __DSB();
  __ISB();
}

void pbl_soc_early_init(void) {
  SystemInit();
  t5ai_reset_reason_capture_boot();
  t5ai_reboot_trace_capture_boot();
  icache_enable();
  dcache_enable();

  /* The pinned CP waits for this AP indication and otherwise resets the
   * system after four start retries. This is the vendor control-channel wire
   * format, not a dependency on its scheduler or IPC implementation. */
  t5ai_cp_mailbox_announce_power_up();

  /* Do not immediately fill the shared FIFO with a second request. Give CP0
   * time to consume the startup indication before requesting UART0 power. */
  volatile uint32_t *const rtc_low = (volatile uint32_t *)0x4400020cu;
  const uint32_t settle_start = *rtc_low;
  while ((uint32_t)(*rtc_low - settle_start) < 3200u) {
  }

  delay_init();
  __enable_irq();
}
