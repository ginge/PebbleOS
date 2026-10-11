# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual board-local sampler and event boundaries on the host."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {uint32_t ticks;} pbl_timeout_t;
struct pbl_thread {struct {uint32_t run_time,switched_in_at;} backend; uint32_t id;};
struct pbl_msgq {struct {uint32_t count;} backend;};
typedef struct {unsigned type; struct {void (*callback)(void *);} callback;} PebbleEvent;
typedef struct {struct pbl_msgq *to_process_event_queue;} ProcessContext;
#define PebbleTask_App 2
#define PEBBLE_CALLBACK_EVENT 27
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_DBG(...) probe_log(__VA_ARGS__)
void probe_log(const char *,...);
void pbl_irq_lock(void); void pbl_irq_unlock(void);
uint32_t pbl_uptime_ticks(void);
struct pbl_thread *pebble_task_get_thread(int);
ProcessContext *app_manager_get_task_context(void);
"""
HARNESS = r"""
#include <assert.h>
#include <sys/mman.h>
#include PROFILE_SOURCE
static struct pbl_thread app={.id=42},other={.id=43};
struct pbl_thread *pbl_cur=&app;
static struct pbl_msgq queue,unrelated;
static ProcessContext context={&queue};
static unsigned depth,logs,receives;
void pbl_irq_lock(void) {++depth;}
void pbl_irq_unlock(void) {assert(depth);--depth;}
void probe_log(const char *fmt,...) {(void)fmt;assert(!depth);++logs;}
uint32_t pbl_uptime_ticks(void) {return T5AI_AON_RTC_COUNT_LOW/32u;}
struct pbl_thread *pebble_task_get_thread(int task) {assert(task==2);return &app;}
ProcessContext *app_manager_get_task_context(void) {return &context;}
int __real_pbl_msgq_get(struct pbl_msgq *q,void *msg,pbl_timeout_t t) {
 (void)q;(void)t;assert(!depth);++receives;
 T5AI_AON_RTC_COUNT_LOW+=6400u; // Queue receive plus ready delay, not processing.
 ((PebbleEvent *)msg)->type=PEBBLE_CALLBACK_EVENT;
 ((PebbleEvent *)msg)->callback.callback=(void *)0x2123457u;
 return 0;
}
int main(void) {
 assert(mmap((void *)0x44000000u,0x10000,PROT_READ|PROT_WRITE,
 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
 assert(mmap((void *)0x28010000u,0x56000,PROT_READ|PROT_WRITE,
 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
 assert(prv_frame_valid(0x28010000u,0xfffffffdu));
 assert(prv_frame_valid(0x28065f98u,0xffffffedu));
 assert(!prv_frame_valid(0x28065f9cu,0xffffffedu));
 assert(!prv_frame_valid(0x28065fe4u,0xfffffffdu));
 assert(!prv_frame_valid(0x28010001u,0xfffffffdu));
 assert(!prv_frame_valid(0x28010000u,0xfffffff9u));
 assert(!prv_frame_valid(0x28010000u,0xfffffff1u));
 uint32_t *f=(void *)0x28010000u;
 f[6]=0x02123456u;f[7]=1u<<24;
 T5AI_AON_RTC_COUNT_LOW=0xf0000000u;
 t5ai_app_trace_tick(f,0xfffffffdu);assert(!s_samples);
 memset(s_buckets,0xff,sizeof(s_buckets));
 t5ai_app_trace_dump();assert(s_storage_ready && !s_buckets[0].count);
 t5ai_app_trace_tick(f,0xfffffffdu);assert(s_samples==1u);
 t5ai_app_trace_tick(f,0xfffffffdu);assert(s_samples==1u);
 T5AI_AON_RTC_COUNT_LOW+=500u;f[7]|=1u;
 t5ai_app_trace_tick(f,0xfffffffdu);assert(s_samples==1u);
 T5AI_AON_RTC_COUNT_LOW+=500u;f[7]=1u<<24;pbl_cur=&other;
 t5ai_app_trace_tick(f,0xfffffffdu);assert(s_samples==1u);
 pbl_cur=&app;PebbleEvent e={0};
 __wrap_pbl_msgq_get(&unrelated,&e,(pbl_timeout_t){0});assert(!s_events);
 __wrap_pbl_msgq_get(&queue,&e,(pbl_timeout_t){0});
 assert(s_events==1u && s_callback==0x2123457u && s_waits==6400u);
 s_read=s_write;uint32_t start=T5AI_AON_RTC_COUNT_LOW;
 T5AI_AON_RTC_COUNT_LOW+=9600u;
 __wrap_pbl_msgq_get(&queue,&e,(pbl_timeout_t){0});
 assert(s_history[s_read%HISTORY].wall==9600u);
 assert(s_history[s_read%HISTORY].cpu==300u);
 assert(s_history[s_read%HISTORY].callback==0x2123457u);
 assert(T5AI_AON_RTC_COUNT_LOW-start==16000u);
 for(unsigned i=0;i<HISTORY+4u;++i) {
  T5AI_AON_RTC_COUNT_LOW+=3200u;
  __wrap_pbl_msgq_get(&queue,&e,(pbl_timeout_t){0});
 }
 assert(s_write-s_read==HISTORY && s_lost==5u);
 // Bounded collision overflow is counted, rather than misattributed.
 memset(s_buckets,0,sizeof(s_buckets));s_samples=s_overflow=0u;
 for(unsigned i=0;i<5u;++i) {
  f[6]=0x02123000u+i*BUCKETS*16u;
  T5AI_AON_RTC_COUNT_LOW+=500u;t5ai_app_trace_tick(f,0xfffffffdu);
 }
 assert(s_samples==5u && s_overflow==1u);
 t5ai_app_trace_dump();assert(logs && !s_samples && !s_overflow);
 assert(s_read==s_write && !depth && receives==HISTORY+7u);
 return 0;
}
"""

class AppTraceTests(unittest.TestCase):
    def test_parser_keeps_receive_and_processing_separate(self):
        from tools.unhygienix_app_trace import summarize
        report = summarize("""T5APP id=42 event=27 cb=02123457 age_ms=300 waiting=0 samples=90 overflow=2
T5APP now=64000 events=3 receive_ms=210 history_lost=0
T5APP pc=02123450 event=27 count=60
T5APP long id=42 event=27 cb=02123457 wall_ms=300 cpu_ticks=250 queued=2
""")
        self.assertEqual(report["samples_in_complete_headers"], 90)
        self.assertEqual(report["windows"][0]["receive_ms"], 210)
        self.assertEqual(report["long_events"][0]["wall_ms"], 300)
        self.assertEqual(report["top_pc_buckets"][0]["pc"], 0x02123450)

    def test_actual_sampler_and_event_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "stub.h").write_text(STUB)
            for name in ["pbl/kernel/irq.h", "pbl/kernel/msgq.h", "pbl/kernel/thread.h",
                         "pbl/kernel/sched.h", "pbl/logging/logging.h", "kernel/pebble_tasks.h",
                         "process_management/app_manager.h"]:
                target = d / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "stub.h"\n')
            (d / "test.c").write_text(HARNESS)
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-no-pie",
                            "-DCONFIG_T5AI_APP_TRACE=1", "-I", str(d), "-I", str(ROOT / "include"),
                            f'-DPROFILE_SOURCE="{ROOT / "soc/t5ai/app_trace.c"}"',
                            str(d / "test.c"), "-o", str(d / "test")],
                           check=True, capture_output=True)
            subprocess.run([str(d / "test")], check=True)

if __name__ == "__main__":
    unittest.main()
