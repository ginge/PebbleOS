/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * The Waveshare T5-E1 carries 16 MiB of PSRAM at 0x60000000.  Keep the
 * display allocation separate from the vendor slab layout: Pebble does not
 * link the vendor runtime, and the remainder stays unavailable until a
 * native allocator and controller ownership policy exist, except for the
 * explicitly reserved display DMA and SD PFS metadata regions below.
 */
MEMORY
{
    T5AI_DISPLAY_DMA_RAM (rw) : ORIGIN = 0x60024000, LENGTH = 16K
    T5AI_DISPLAY_RAM (rw) : ORIGIN = 0x60000000, LENGTH = 128K
#ifdef CONFIG_T5AI_SD_PFS
    /* Native AP replaces vendor audio/encode slabs; CP heap begins at 0x60700000. */
    T5AI_PFS_RAM (rw) : ORIGIN = 0x60020000, LENGTH = 8K
#endif
}
