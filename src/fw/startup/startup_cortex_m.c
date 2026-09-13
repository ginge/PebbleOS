/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Initial firmware startup, contains the vector table that the bootloader loads.
//! Based on "https://github.com/pfalcon/cortex-uni-startup/blob/master/startup.c"
//! by Paul Sokolovsky (public domain)

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "pbl/mcu/cache.h"
#include "pbl/util/attributes.h"

//! These symbols are defined in the linker script for use in initializing
//! the data sections. uint8_t since we do arithmetic with section lengths.
//! These are arrays to avoid the need for an & when dealing with linker symbols.
extern uint8_t __data_load_start[];
extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
extern uint8_t _estack[];

#if defined(CONFIG_SOC_T5AI)
extern uint8_t __t5ai_ramfunc_load_start[];
extern uint8_t __t5ai_ramfunc_start[];
extern uint8_t __t5ai_ramfunc_end[];
#endif

//! Firmware main function, ResetHandler calls this
extern int main(void);

//! SoC-specific system initialization (e.g. nRF52 system_nrf52840.c)
extern void SystemInit(void);

//! This function is what gets called when the processor first
//! starts execution following a reset event. The data and bss
//! sections are initialized, then we call the firmware's main
//! function
NORETURN Reset_Handler(void) {
#if defined(CONFIG_SOC_T5AI)
  /* Preserve the bounded reset-stage UART probe used by the first native
   * image that completed the pinned CP's CPU1 startup handshake. Besides
   * providing a pre-RAM execution marker, the status read synchronizes AP
   * startup with the UART state inherited from TuyaBoot. */
  volatile uint32_t *const uart0 = (volatile uint32_t *)0x44820000u;
  for (unsigned timeout = 0; timeout < 1000000u; ++timeout) {
    if (uart0[6] & (1u << 20)) {
      uart0[7] = 'A';
      break;
    }
  }
#endif

  // Copy data section from flash to RAM
  memcpy(__data_start, __data_load_start, __data_end - __data_start);
#if defined(CONFIG_SOC_T5AI)
  memcpy(__t5ai_ramfunc_start, __t5ai_ramfunc_load_start,
         __t5ai_ramfunc_end - __t5ai_ramfunc_start);
#endif

  // Clear the bss section, assumes .bss goes directly after .data
  memset(__bss_start, 0, __bss_end - __bss_start);

  SystemInit();

  icache_enable();
  dcache_enable();

  main();

  // Main shouldn't return
  while (true) {
  }
}
