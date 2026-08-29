/* SPDX-License-Identifier: Apache-2.0 */
/* The T5AI AP bootloader maps the CRC-stripped application partition here. */
ASSERT(ORIGIN(FLASH) == 0x02120000, "Unexpected T5AI AP virtual flash base")
ASSERT(ORIGIN(KERNEL_RAM) == 0x28010000, "Unexpected T5AI AP RAM base")
ASSERT(ORIGIN(APP_RAM) + LENGTH(APP_RAM) <= 0x28066000,
       "Pebble memory overlaps the T5AI communications-processor RAM")
