/* SPDX-License-Identifier: Apache-2.0 */
/* The T5AI AP bootloader maps the CRC-stripped application partition here. */
ASSERT(ORIGIN(FLASH) == 0x02120000, "Unexpected T5AI AP virtual flash base")
ASSERT(ORIGIN(KERNEL_RAM) == 0x28010000, "Unexpected T5AI AP RAM base")
ASSERT(ORIGIN(APP_RAM) + LENGTH(APP_RAM) <= 0x28066000,
       "Pebble memory overlaps the T5AI communications-processor RAM")
ASSERT(ORIGIN(T5AI_DISPLAY_RAM) == 0x60000000,
       "Unexpected T5AI PSRAM base")
ASSERT(LENGTH(T5AI_DISPLAY_RAM) <= 0x00020000,
       "Display reservation exceeds the first T5AI PSRAM slab")
ASSERT(__t5ai_framebuffer_end__ <= ORIGIN(T5AI_DISPLAY_RAM) + LENGTH(T5AI_DISPLAY_RAM),
       "T5AI framebuffer exceeds its PSRAM reservation")
