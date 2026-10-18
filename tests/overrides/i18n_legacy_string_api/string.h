/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include_next <string.h>

// Match the embedded libc's signature instead of glibc's C23 generic macro.
#undef strstr
