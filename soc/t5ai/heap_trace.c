/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/soc/t5ai/heap_trace.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/util/heap.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include "kernel/kernel_heap.h"
#include "system/passert.h"
#include <inttypes.h>

PBL_LOG_MODULE_DEFINE(t5ai_heap_trace, LOG_LEVEL_DEBUG);
#define RECORD_COUNT 16u
typedef struct {
  uint32_t sequence, rtc_ms, thread, operation, bytes;
  uintptr_t client, site, pointer;
} Record;
static Record s_records[RECORD_COUNT];
static uint32_t s_next, s_last_dump, s_omitted;
static bool s_dumping;
static uint32_t prv_now(void) {
  return (uint32_t)(t5ai_rtc_get_raw_ticks() / (T5AI_AON_RTC_HZ / 1000u));
}
static void prv_record(Heap *heap, uint32_t op, size_t bytes, uintptr_t client, uintptr_t site,
                       void *pointer) {
  if (heap != kernel_heap_get())
    return;
  pbl_irq_lock();
  if (!s_dumping) {
    struct pbl_thread *thread = pbl_thread_current();
    uint32_t sequence = ++s_next;
    s_records[(sequence - 1u) % RECORD_COUNT] = (Record){
      .sequence = sequence,
      .rtc_ms = prv_now(),
      .thread = thread ? thread->id : 0,
      .operation = op,
      .bytes = bytes,
      .client = client,
      .site = site,
      .pointer = (uintptr_t)pointer,
    };
  } else {
    s_omitted++;
  }
  pbl_irq_unlock();
}
// The call site is a fallback when the allocator's supplied client PC is zero.
#define SITE ((uintptr_t)__builtin_return_address(0))
void *__real_heap_malloc(Heap *, unsigned long, uintptr_t);
void *__wrap_heap_malloc(Heap *heap, unsigned long bytes, uintptr_t client) {
  void *ptr = __real_heap_malloc(heap, bytes, client);
  prv_record(heap, 1, bytes, client, SITE, ptr);
  return ptr;
}
void *__real_heap_zalloc(Heap *, size_t, uintptr_t);
void *__wrap_heap_zalloc(Heap *heap, size_t bytes, uintptr_t client) {
  void *ptr = __real_heap_zalloc(heap, bytes, client);
  prv_record(heap, 2, bytes, client, SITE, ptr);
  return ptr;
}
void *__real_heap_calloc(Heap *, size_t, size_t, uintptr_t);
void *__wrap_heap_calloc(Heap *heap, size_t count, size_t bytes, uintptr_t client) {
  void *ptr = __real_heap_calloc(heap, count, bytes, client);
  prv_record(heap, 3, count * bytes, client, SITE, ptr);
  return ptr;
}
void *__real_heap_realloc(Heap *, void *, unsigned long, uintptr_t);
void *__wrap_heap_realloc(Heap *heap, void *old, unsigned long bytes, uintptr_t client) {
  void *ptr = __real_heap_realloc(heap, old, bytes, client);
  prv_record(heap, 4, bytes, client, SITE, ptr);
  return ptr;
}
void __real_heap_free(Heap *, void *, uintptr_t);
void __wrap_heap_free(Heap *heap, void *ptr, uintptr_t client) {
  __real_heap_free(heap, ptr, client);
  prv_record(heap, 5, 0, client, SITE, ptr);
}
static void prv_dump(bool fatal) {
  pbl_irq_lock();
  if (s_dumping) {
    pbl_irq_unlock();
    return;
  }
  s_dumping = true;
  uint32_t end = s_next, omitted = s_omitted;
  pbl_irq_unlock();
  unsigned used, free_bytes, largest;
  heap_calc_totals(kernel_heap_get(), &used, &free_bytes, &largest);
  PBL_LOG_DBG("T5HEAP fatal=%u rtc_ms=%" PRIu32 " used=%u free=%u largest=%u next=%" PRIu32, fatal,
              prv_now(), used, free_bytes, largest, end);
  PBL_LOG_DBG("T5HEAP omitted_during_dump=%" PRIu32, omitted);
  if (fatal || free_bytes < 2048 || largest < 512) {
    uint32_t count = end < RECORD_COUNT ? end : RECORD_COUNT;
    for (uint32_t i = 0; i < count; ++i) {
      const Record *record = &s_records[(end - count + i) % RECORD_COUNT];
      PBL_LOG_DBG("T5HEAP seq=%" PRIu32 " rtc_ms=%" PRIu32 " thread=%" PRIu32 " op=%" PRIu32
                  " bytes=%" PRIu32 " ptr=%" PRIxPTR,
                  record->sequence, record->rtc_ms, record->thread, record->operation,
                  record->bytes, record->pointer);
      PBL_LOG_DBG("T5HEAP seq=%" PRIu32 " client=%" PRIxPTR " site=%" PRIxPTR, record->sequence,
                  record->client, record->site);
    }
  }
  pbl_irq_lock();
  s_dumping = false;
  pbl_irq_unlock();
}
void t5ai_heap_trace_dump(void) {
  uint32_t now = prv_now();
  if (now - s_last_dump < 10000u)
    return;
  s_last_dump = now;
  prv_dump(false);
}
PBL_NORETURN void __real_croak_oom(size_t, int, Heap *);
PBL_NORETURN void __wrap_croak_oom(size_t bytes, int client, Heap *heap) {
  if (heap == kernel_heap_get())
    prv_dump(true);
  __real_croak_oom(bytes, client, heap);
}
