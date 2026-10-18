/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * The Waveshare T5-E1 carries 16 MiB of PSRAM at 0x60000000.  Keep the
 * display allocation separate from the vendor slab layout: Pebble does not
 * link the vendor runtime, and the remainder stays unavailable until a
 * native allocator and controller ownership policy exist.
 */
MEMORY
{
    T5AI_DISPLAY_RAM (rw) : ORIGIN = 0x60000000, LENGTH = 128K
}
