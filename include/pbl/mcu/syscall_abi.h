/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* All syscall stubs and the exception handler must use the same build profile. */
#if defined(PBL_RTOS_TUYA) && PBL_RTOS_TUYA
#define PBL_SYSCALL_SVC 64
#define PBL_SYSCALL_TLS_BASE 3
#else
#define PBL_SYSCALL_SVC 2
#define PBL_SYSCALL_TLS_BASE 0
#endif

#define PBL_SYSCALL_TLS_LR PBL_SYSCALL_TLS_BASE
#define PBL_SYSCALL_TLS_SP (PBL_SYSCALL_TLS_BASE + 1)
#define PBL_SYSCALL_TLS_REQUIRED (PBL_SYSCALL_TLS_BASE + 2)
#define PBL_SYSCALL_STRINGIFY_(value) #value
#define PBL_SYSCALL_STRINGIFY(value) PBL_SYSCALL_STRINGIFY_(value)
#define PBL_SYSCALL_ASM "svc " PBL_SYSCALL_STRINGIFY(PBL_SYSCALL_SVC) "\n"
