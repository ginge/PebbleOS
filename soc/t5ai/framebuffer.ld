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
        KEEP(*(.t5ai_board_cache))
        __t5ai_board_cache_end__ = .;
    } >T5AI_DISPLAY_RAM
