/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

ENTRY(reset_handler)

MEMORY {
  FLASH (rx) : ORIGIN = 0x02120000, LENGTH = 4K
}

SECTIONS {
  .isr_vector ORIGIN(FLASH) : {
    KEEP(*(.isr_vector))
  } > FLASH

  .text : {
    *(.text*)
    *(.rodata*)
  } > FLASH

  /DISCARD/ : {
    *(.ARM.attributes)
    *(.comment)
    *(.note*)
  }
}

ASSERT(SIZEOF(.isr_vector) == 0x100,
       "T5AI CPU1 probe must contain the complete 64-entry vector table")
ASSERT(SIZEOF(.isr_vector) + SIZEOF(.text) <= LENGTH(FLASH),
       "T5AI CPU1 probe exceeds its isolated 4 KiB link region")
