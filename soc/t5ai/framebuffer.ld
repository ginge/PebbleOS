/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

    .t5ai_framebuffer (NOLOAD) : ALIGN(32) {
        __t5ai_framebuffer_start__ = .;
        KEEP(*(.t5ai_framebuffer))
        __t5ai_framebuffer_end__ = .;
    } >T5AI_DISPLAY_RAM

    /* Board-local caches that must not reduce the kernel heap live in the
     * remaining PSRAM aperture, outside the framebuffer validation range. */
    .t5ai_board_cache (NOLOAD) : ALIGN(32) {
        __t5ai_board_cache_start__ = .;
        /* These are AP-only NimBLE pool backing arrays. The T5AI HCI driver
         * copies packets at the CP ownership boundary, so neither array is
         * dereferenced by CPU0. Their pool metadata stays zero-initialized in
         * internal RAM and os_mempool_init() initializes every backing block
         * before Bluetooth starts using it. Keep the proven pool counts while
         * moving their 19,820-byte capacity out of the kernel heap region. */
        __t5ai_msys_pool_start__ = .;
        KEEP(*(.bss.os_msys_1_data))
        __t5ai_msys_pool_end__ = .;
        __t5ai_acl_pool_start__ = .;
        KEEP(*(.bss.pool_acl_buf))
        __t5ai_acl_pool_end__ = .;
        KEEP(*(.t5ai_board_cache))
        __t5ai_board_cache_end__ = .;
    } >T5AI_DISPLAY_RAM

#ifdef CONFIG_T5AI_SD_PFS
    .t5ai_pfs_flags (NOLOAD) : ALIGN(32) {
        __t5ai_pfs_flags_start__ = .;
        KEEP(*(.t5ai_pfs_flags))
        __t5ai_pfs_flags_end__ = .;
    } >T5AI_PFS_RAM
#endif

    .t5ai_display_dma (NOLOAD) : ALIGN(32) {
        __t5ai_display_dma_start__ = .;
        KEEP(*(.t5ai_display_dma))
        __t5ai_display_dma_end__ = .;
    } >T5AI_DISPLAY_DMA_RAM

    /* Private upstream names remain a reviewed contract until NimBLE exposes
     * backing-storage hooks. Fail on renames, removal or capacity drift. */
    ASSERT(__t5ai_msys_pool_end__ - __t5ai_msys_pool_start__ == 0x27ec,
           "T5AI NimBLE MSYS pool missing or resized; review PSRAM placement")
    ASSERT(__t5ai_acl_pool_end__ - __t5ai_acl_pool_start__ == 0x2580,
           "T5AI NimBLE ACL pool missing or resized; review PSRAM placement")
