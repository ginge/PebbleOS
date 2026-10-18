/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

// Codec state and voice frame buffers must use the same selected backend.
// Checked allocations assert on failure; zalloc also clears the allocation.
void *pbl_speex_malloc_check(size_t size);
void *pbl_speex_zalloc_check(size_t size);
// A failed realloc returns NULL and leaves ptr allocated and unchanged.
void *pbl_speex_realloc(void *ptr, size_t size);
// Accepts NULL. Otherwise ptr must belong to this backend.
void pbl_speex_free(void *ptr);
