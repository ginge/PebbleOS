/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
    .t5ai_retained (NOLOAD) : ALIGN(8) {
        KEEP(*(.t5ai_retained))
    } >RAM

#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
    .t5ai_sleep_trace (NOLOAD) : ALIGN(32) {
        KEEP(*(.t5ai_sleep_trace))
    } >RAM
#endif
