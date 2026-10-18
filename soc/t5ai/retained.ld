/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
    .t5ai_retained (NOLOAD) : ALIGN(8) {
        KEEP(*(.t5ai_retained))
    } >RAM
