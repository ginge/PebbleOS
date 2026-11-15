/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "pbl/kernel/malloc.h"

#include <pbl/util/speex_memory.h>

void *pbl_speex_malloc_check(size_t size) {
  return pbl_malloc_check(size);
}

void *pbl_speex_zalloc_check(size_t size) {
  return pbl_zalloc_check(size);
}

void *pbl_speex_realloc(void *ptr, size_t size) {
  return pbl_realloc(ptr, size);
}

void pbl_speex_free(void *ptr) {
  pbl_free(ptr);
}
