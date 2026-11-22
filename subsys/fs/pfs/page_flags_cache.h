/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_FS_PFS_CUSTOM_PAGE_FLAGS
// Used only for the page-flags cache; ordinary allocations use the heap.
// Rebuild releases the previous cache before requesting the new byte count.
uint8_t *pfs_page_flags_cache_alloc(size_t bytes);
void pfs_page_flags_cache_free(uint8_t *cache);
#else
#include <pbl/kernel/malloc.h>

static inline uint8_t *pfs_page_flags_cache_alloc(size_t bytes) {
  return pbl_malloc_check(bytes);
}

static inline void pfs_page_flags_cache_free(uint8_t *cache) {
  pbl_free(cache);
}
#endif
