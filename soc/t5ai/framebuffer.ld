/* SPDX-License-Identifier: Apache-2.0 */

    .t5ai_framebuffer (NOLOAD) : ALIGN(32) {
        __t5ai_framebuffer_start__ = .;
        KEEP(*(.t5ai_framebuffer))
        __t5ai_framebuffer_end__ = .;
    } >T5AI_DISPLAY_RAM
