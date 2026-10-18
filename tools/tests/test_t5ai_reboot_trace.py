# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tests.test_t5ai_state_trace import HEADERS as STATE_HEADERS

ROOT = Path(__file__).resolve().parents[2]
HEADERS = dict(STATE_HEADERS)
HEADERS['pbl/soc/t5ai/rtc.h'] = '''
#include <stdint.h>
#define T5AI_AON_RTC_HZ 32000u
uint64_t t5ai_rtc_get_raw_ticks(void);
'''
HARNESS = r'''
#include <assert.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include TRACE_SOURCE
static unsigned depth, flushes, logs, previous_logs, warnings, creates;
static unsigned early_reports, board_reports, reason_changes;
static uint32_t ticks;
static uint64_t raw_ticks;
static jmp_buf drained;
void pbl_irq_lock(void) { ++depth; }
void pbl_irq_unlock(void) { assert(depth); --depth; }
uint32_t pbl_uptime_ticks(void) { return ticks; }
uint64_t t5ai_rtc_get_raw_ticks(void) { return raw_ticks; }
void dcache_flush(const void *p, size_t n) { assert(p && n); ++flushes; }
void probe_log(const char *fmt, ...) {
  assert(depth == 0);
  char line[256];
  va_list args; va_start(args,fmt); vsnprintf(line,sizeof(line),fmt,args); va_end(args);
  ++logs;
  early_reports += strstr(line,"stage=soc r0=72000150 r7a=62000120") != NULL;
  board_reports += strstr(line,"stage=board r0=00000000 r7a=62000120") != NULL;
  reason_changes += strstr(line,"reason change r0=000000ab r7a=62000120") != NULL;
  previous_logs += strstr(line,"previous=1") != NULL;
  warnings += strstr(line,"invalid") != NULL || strstr(line,"lost=") != NULL;
}
void pbl_thread_sleep(unsigned t) { assert(t == 2000); longjmp(drained,1); }
int pbl_thread_create(struct pbl_thread *t, const struct pbl_thread_attr *a) {
  assert(t && a->prio == 1 && a->stack_size == 1536);
  ++creates; return 0;
}
int main(void) {
  assert(mmap((void *)0x44000000u, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  assert(mmap((void *)0x2809f000u, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  T5AI_TRACE_CP_SAVED = 7; T5AI_TRACE_AP_SAVED = 2;
  T5AI_TRACE_PMU_LIVE = 0x72000150;
  T5AI_TRACE_PMU_BOOT = 0x62000120;
  t5ai_reboot_trace_capture_boot();
  assert(s_boot_early.pmu_live == 0x72000150);
  assert(s_boot_early.pmu_boot == 0x62000120);
  assert(s_boot_early.cp_saved == 7 && s_boot_early.ap_saved == 2);
  raw_ticks = 100 * 32; t5ai_reboot_trace_watchdog(true);
  raw_ticks = 350 * 32; t5ai_reboot_trace_watchdog(true);
  assert(s_watchdog.count == 2 && s_watchdog.max_gap == 250);
  t5ai_reboot_trace_watchdog(false);
  raw_ticks = 10000 * 32; t5ai_reboot_trace_watchdog(true);
  assert(s_watchdog.count == 3 && s_watchdog.max_gap == 250);
  T5AI_TRACE_PMU_LIVE = 0;
  t5ai_reboot_trace_capture_boot(); // Capture once, before later clearing.
  assert(s_boot_early.pmu_live == 0x72000150);
  assert(sizeof(Trace) == 776);
  t5ai_reboot_trace_event(T5RebootTrace_ForceReset,123);
  assert(!flushes && !logs);
  t5ai_reboot_trace_start();
  assert(s_boot_board.pmu_live == 0 && s_boot_early.pmu_live == 0x72000150);
  assert(creates == 1 && !logs); // Starting and hooks never log.
  t5ai_reboot_trace_start();
  assert(creates == 1);
  ticks = UINT32_MAX - 5;
  raw_ticks = (uint64_t)1234 * 32;
  t5ai_reboot_trace_event(T5RebootTrace_HeartbeatSend,(uint32_t)-9);
  assert(s_trace.records[0].rtc_ms == 1234);
  assert(s_trace.records[0].ticks == UINT32_MAX - 5);
  assert(s_trace.records[0].value == (uint32_t)-9);
  for (unsigned i=0; i<100; ++i) {
    ticks += 1000; raw_ticks += 32000;
    t5ai_reboot_trace_event(T5RebootTrace_HeartbeatAck,i);
  }
  assert(!logs && depth == 0 && flushes == 203);
  T5AI_TRACE_PMU_LIVE = 0xab;
  if (!setjmp(drained)) prv_entry(NULL);
  assert(early_reports == 1 && board_reports == 1 && reason_changes == 1);
  assert(warnings == 1); // Bounded overrun is visible.
  assert(s_trace.next_sequence == 102);
  // Simulate reset while the NOLOAD ring survives, then an early new event.
  s_started = false; s_live = false; s_boot_captured = false;
  t5ai_reboot_trace_capture_boot();
  assert(s_previous.next_sequence == 102);
  s_trace.magic = 0; // Simulate startup destroying the live ring after capture.
  logs = previous_logs = warnings = 0;
  t5ai_reboot_trace_start();
  assert(!logs && s_trace.next_sequence == 1);
  t5ai_reboot_trace_event(T5RebootTrace_ReasonSet,17);
  assert(s_previous.next_sequence == 102 && s_boot_board.magic == 0);
  if (!setjmp(drained)) prv_entry(NULL);
  assert(previous_logs == 32 && !warnings);
  // Reject torn records and invalid retention, never invent a reset cause.
  s_previous.records[100 % TRACE_COUNT].checksum ^= 1;
  warnings = 0; prv_dump_previous(); assert(warnings == 1);
  s_previous.magic = 0; previous_logs = 0;
  prv_dump_previous(); assert(!previous_logs);
  return 0;
}
'''


class RebootTraceTest(unittest.TestCase):
    def test_bounded_retention_replay_and_nonlogging_hooks(self):
        with tempfile.TemporaryDirectory() as temporary:
            d = Path(temporary)
            for name, contents in HEADERS.items():
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(contents)
            (d / 'test.c').write_text(HARNESS)
            exe = d / 'test'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-DCONFIG_T5AI_REBOOT_TRACE=1', '-I', str(d),
                            '-I', str(ROOT / 'include'),
                            f'-DTRACE_SOURCE="{ROOT / "soc/t5ai/reboot_trace.c"}"',
                            str(d / 'test.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_disabled_header_needs_no_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            d = Path(temporary)
            (d / 'test.c').write_text('''
#include <pbl/soc/t5ai/reboot_trace.h>
#include <pbl/soc/t5ai/heap_trace.h>
int main(void) {
 t5ai_heap_trace_dump();
 t5ai_reboot_trace_watchdog(true);
 t5ai_reboot_trace_capture_boot();
 t5ai_reboot_trace_start();
 t5ai_reboot_trace_event(T5RebootTrace_ForceReset, 1);
 return 0;
}
''')
            exe = d / 'test'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I',
                            str(ROOT / 'include'), str(d / 'test.c'),
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
