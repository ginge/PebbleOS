/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "system/passert.h"

#include <pbl/kernel/mutex.h>
#include <pbl/soc/t5ai/voice_memory.h>
#include <pbl/util/heap.h>
#include <pbl/util/speex_memory.h>

static unsigned char s_arena[T5AI_VOICE_ARENA_BYTES]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));
static Heap s_heap;
static PBL_MUTEX_DEFINE(s_mutex);
static bool s_initialized;

static void prv_init(void) {
  if (!s_initialized) {
    heap_init(&s_heap, s_arena, s_arena + sizeof(s_arena), false);
    s_initialized = true;
  }
}

void *pbl_speex_malloc_check(size_t size) {
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  prv_init();
  void *ptr = heap_malloc(&s_heap, size, 0u);
  pbl_mutex_unlock(&s_mutex);
  PBL_ASSERTN(ptr);
  return ptr;
}

void *pbl_speex_zalloc_check(size_t size) {
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  prv_init();
  void *ptr = heap_zalloc(&s_heap, size, 0u);
  pbl_mutex_unlock(&s_mutex);
  PBL_ASSERTN(ptr);
  return ptr;
}

void *pbl_speex_realloc(void *ptr, size_t size) {
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  prv_init();
  void *result = heap_realloc(&s_heap, ptr, size, 0u);
  pbl_mutex_unlock(&s_mutex);
  return result;
}

void pbl_speex_free(void *ptr) {
  if (!ptr) {
    return;
  }
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  PBL_ASSERTN(s_initialized && heap_contains_address(&s_heap, ptr));
  heap_free(&s_heap, ptr, 0u);
  pbl_mutex_unlock(&s_mutex);
}
