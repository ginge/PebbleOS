/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "kernel/pbl_malloc.h"

#include <pbl/util/speex_memory.h>

void *pbl_speex_malloc_check(size_t size) {
  return kernel_malloc_check(size);
}

void *pbl_speex_zalloc_check(size_t size) {
  return kernel_zalloc_check(size);
}

void *pbl_speex_realloc(void *ptr, size_t size) {
  return kernel_realloc(ptr, size);
}

void pbl_speex_free(void *ptr) {
  kernel_free(ptr);
}
