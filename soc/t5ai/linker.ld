/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
/* The T5AI AP bootloader maps the CRC-stripped application partition here. */
ASSERT(ORIGIN(FLASH) == 0x02120000, "Unexpected T5AI AP virtual flash base")
ASSERT(ORIGIN(FLASH) + LENGTH(FLASH) == 0x022b0000,
       "T5AI firmware image is not bounded by AP slot 0")
ASSERT(ORIGIN(RAM) == 0x28010000, "Unexpected T5AI AP RAM base")
ASSERT(__APP_RAM_end__ <= 0x28066000,
       "Pebble memory overlaps the T5AI communications-processor RAM")
ASSERT(ORIGIN(T5AI_DISPLAY_RAM) == 0x60000000,
       "Unexpected T5AI PSRAM base")
ASSERT(LENGTH(T5AI_DISPLAY_RAM) <= 0x00020000,
       "Display reservation exceeds the first T5AI PSRAM slab")
ASSERT(__t5ai_framebuffer_end__ <= ORIGIN(T5AI_DISPLAY_RAM) + LENGTH(T5AI_DISPLAY_RAM),
       "T5AI framebuffer exceeds its PSRAM reservation")
ASSERT(__t5ai_board_cache_end__ <= ORIGIN(T5AI_DISPLAY_RAM) + LENGTH(T5AI_DISPLAY_RAM),
       "T5AI board cache exceeds its PSRAM reservation")

#ifdef CONFIG_T5AI_SD_PFS
ASSERT(ORIGIN(T5AI_PFS_RAM) == 0x60020000 && LENGTH(T5AI_PFS_RAM) == 8192,
       "Unexpected SD PFS metadata reservation")
ASSERT(__t5ai_pfs_flags_start__ == ORIGIN(T5AI_PFS_RAM) &&
       __t5ai_pfs_flags_end__ == ORIGIN(T5AI_PFS_RAM) + LENGTH(T5AI_PFS_RAM),
       "SD PFS flags must fill their private PSRAM reservation")
#endif
