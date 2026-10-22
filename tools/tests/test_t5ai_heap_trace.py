# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the production bounded heap hooks, without allocator policy changes."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tests.test_t5ai_reminder_profile import STUB

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "stub.h"
#include <assert.h>
#include <stdarg.h>
#include <setjmp.h>
#include "soc/t5ai/heap_trace.c"
static Heap kernel, app;
static struct pbl_thread thread = {.id = 42};
static unsigned depth, logs, scans, allocations, croaks;
static uint64_t raw;
static bool fail;
static jmp_buf died;
Heap *kernel_heap_get(void) { return &kernel; }
struct pbl_thread *pbl_thread_current(void) { return &thread; }
uint64_t t5ai_rtc_get_raw_ticks(void) { return raw; }
void pbl_irq_lock(void) { ++depth; }
void pbl_irq_unlock(void) { assert(depth); --depth; }
void test_log(const char *format, ...) {
  assert(!depth); (void)format; ++logs;
  // Logging's own allocations are intentionally excluded from history.
  prv_record(&kernel, 1, 8, 1, 2, &thread);
}
void heap_calc_totals(Heap *h, unsigned *used, unsigned *free, unsigned *largest) {
  assert(h == &kernel && !depth); ++scans; *used=24000; *free=1000; *largest=40;
}
void *__real_heap_malloc(Heap *h, unsigned long n, uintptr_t pc) {
  assert(h && n && pc); ++allocations; return fail ? NULL : &kernel;
}
void *__real_heap_zalloc(Heap *h, size_t n, uintptr_t pc) { return __real_heap_malloc(h,n,pc); }
void *__real_heap_calloc(Heap *h, size_t c, size_t n, uintptr_t pc) { return __real_heap_malloc(h,c*n,pc); }
void *__real_heap_realloc(Heap *h, void *p, unsigned long n, uintptr_t pc) {
  assert(p == &kernel); return __real_heap_malloc(h,n,pc);
}
void __real_heap_free(Heap *h, void *p, uintptr_t pc) { assert(h && p && pc); }
[[noreturn]] void __real_pbl_malloc_oom(size_t n, uintptr_t pc, Heap *h) {
  assert(n == 128 && pc == 99 && h == &kernel); ++croaks; longjmp(died,1);
}
int main(void) {
  assert(__wrap_heap_malloc(&app, 128, 99) == &kernel);
  assert(!s_next && !logs && !scans);
  for (unsigned i=0; i<40; ++i) {
    raw += 32;
    assert(__wrap_heap_malloc(&kernel, 128, 99) == &kernel);
  }
  assert(s_next == 40 && !logs && !scans);
  assert(s_records[39 % RECORD_COUNT].sequence == 40);
  assert(s_records[39 % RECORD_COUNT].client == 99);
  assert(s_records[39 % RECORD_COUNT].thread == 42);
  assert(__wrap_heap_zalloc(&kernel, 32, 99) == &kernel);
  assert(__wrap_heap_calloc(&kernel, 4, 32, 99) == &kernel);
  assert(s_records[41 % RECORD_COUNT].bytes == 128);
  assert(__wrap_heap_realloc(&kernel, &kernel, 64, 99) == &kernel);
  __wrap_heap_free(&kernel, &kernel, 99);
  assert(s_next == 44 && !scans && !logs);
  fail = true;
  assert(!__wrap_heap_malloc(&kernel, 128, 99));
  assert(s_records[44 % RECORD_COUNT].pointer == 0 && s_next == 45);
  raw = 10000 * 32; t5ai_heap_trace_dump();
  assert(scans == 1 && logs == 34 && s_next == 45);
  assert(s_omitted == 34);
  raw += 32; t5ai_heap_trace_dump(); assert(scans == 1);
  if (!setjmp(died)) __wrap_pbl_malloc_oom(128,99,&kernel);
  assert(croaks == 1 && scans == 2 && logs == 68 && !s_dumping && !depth);
  return 0;
}
'''

class HeapTraceTest(unittest.TestCase):
    def test_bounded_history_heap_selection_oom_and_rate_limit(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            (d / "stub.h").write_text(STUB + "\ntypedef struct { unsigned current_size; } Heap;\n\nHeap *kernel_heap_get(void);\nvoid heap_calc_totals(Heap *, unsigned *, unsigned *, unsigned *);\nuint64_t t5ai_rtc_get_raw_ticks(void);\n#define T5AI_AON_RTC_HZ 32000u\n")
            for name in ("pbl/soc/t5ai/heap_trace.h", "pbl/soc/t5ai/rtc.h", "pbl/util/heap.h", "pbl/kernel/irq.h", "pbl/kernel/thread.h", "pbl/logging/logging.h", "kernel/kernel_heap.h", "pbl/kernel/assert.h"):
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "stub.h"\n')
            (d / "test.c").write_text(HARNESS)
            exe = d / "test"
            subprocess.run(["cc", "-std=gnu2x", "-Wall", "-Wextra", "-Werror", "-I", str(d), "-I", str(ROOT), str(d / "test.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
