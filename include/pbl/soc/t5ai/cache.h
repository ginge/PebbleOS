/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <pbl/mcu/cache.h>

// Clean arbitrary AP ranges before publishing them to CP. Zero length touches no lines.
static inline void t5ai_cache_clean(const void *address, size_t size) {
  if (size == 0u) {
    return;
  }
  uintptr_t start = (uintptr_t)address;
  dcache_align(&start, &size);
  dcache_flush((const void *)start, size);
}

// Receive only: every intersecting line must be CP-owned, or private padded AP
// storage handed to CP after cleaning. No AP writes are allowed during the lease.
// Never use clean-invalidate here: stale AP bytes would overwrite CP's response.
static inline void t5ai_cache_invalidate_received(void *address, size_t size) {
  if (size == 0u) {
    return;
  }
  uintptr_t start = (uintptr_t)address;
  dcache_align(&start, &size);
  dcache_invalidate((void *)start, size);
}
