# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual KernelBG trace accounting with controlled scheduler boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <assert.h>
#include <stdarg.h>
#include <sys/mman.h>
#include PROFILE_SOURCE
static unsigned depth, logs, real_unlocks;
static void *callback = (void *)0x1234;
static struct pbl_thread bg = {.id=1, .prio=1, .name="KernelBG"};
static struct pbl_thread app = {.id=2, .prio=2, .name="App"};
static struct pbl_mutex mutex = {.owner=&app, .lock_lr=0x5678};
struct pbl_thread *pbl_all_threads = &bg, *pbl_cur = &bg;
struct pbl_thread *sched_switch_in(void) {return pbl_cur;}
void pbl_irq_lock(void) { ++depth; }
void pbl_irq_unlock(void) { assert(depth); --depth; }
uint32_t pbl_uptime_ticks(void) { return T5AI_AON_RTC_COUNT_LOW / 32; }
void *system_task_get_current_callback(void) { return callback; }
struct pbl_thread *pebble_task_get_thread(int task) { (void)task; return &bg; }
int pbl_thread_create(struct pbl_thread *t, const struct pbl_thread_attr *a) {
 (void)t; assert(a->prio == 3 && a->stack_size == 4096); return 0;
}
void pbl_thread_sleep(pbl_timeout_t t) { (void)t; }
void probe_log(const char *fmt, ...) { (void)fmt; assert(!depth); ++logs; }
void __real_system_task_timer_init(void) {}
static void transition(unsigned state, unsigned delta) {
 T5AI_AON_RTC_COUNT_LOW += delta;
 bg.backend.state=state;
 prv_observe();
}
int __real_sched_block(struct pbl_waitq *q, pbl_timeout_t timeout) {
 (void)timeout;
 bg.backend.waitq=q;
 transition(PBL_THREAD_BLOCKED,32);
 transition(PBL_THREAD_READY,9600); // 300 ms blocked.
 bg.backend.waitq=NULL;
 transition(PBL_THREAD_RUNNING,6400); // 200 ms waiting for CPU after wake.
 return -11;
}
int __real_pbl_mutex_lock_lr(struct pbl_mutex *m, pbl_timeout_t t, uintptr_t lr) {
 (void)lr; return __wrap_sched_block(&m->backend.waitq,t);
}
void __real_pbl_mutex_unlock(struct pbl_mutex *m) {
 (void)m; ++real_unlocks;
 transition(PBL_THREAD_READY,32);
 transition(PBL_THREAD_RUNNING,16000); // 500 ms preemption on unlock.
}
int main(void) {
 assert(mmap((void *)0x44000000u,0x10000,PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
 bg.backend.state=PBL_THREAD_RUNNING; s_bg=&bg; s_state=PBL_THREAD_RUNNING; s_callback=(uintptr_t)callback; s_live=true;
 assert(__wrap_pbl_mutex_lock_lr(&mutex,(pbl_timeout_t){1},0x9999)==-11);
 assert(!depth && !logs && !s_op.kind && s_written==2);
 assert(s_ring[0].state==PBL_THREAD_BLOCKED && s_ring[0].span==9600);
 assert(s_ring[0].op.kind==2 && s_ring[0].op.owner==2 && s_ring[0].op.owner_lr==0x5678);
 assert(s_ring[0].op.caller==0x9999 && s_ring[0].op.object==(uintptr_t)&mutex);
 assert(s_ring[1].state==PBL_THREAD_READY && s_ring[1].span==6400);
 assert(s_totals[PBL_THREAD_BLOCKED]==9600 && s_totals[PBL_THREAD_READY]==6400);
 __wrap_pbl_mutex_unlock(&mutex);
 assert(real_unlocks==1 && s_ring[2].op.kind==3 && s_ring[2].span==16000);
 // Ordinary idle poll waits do not become active-stall records.
 callback=NULL; prv_observe(); unsigned before=s_written;
 transition(PBL_THREAD_BLOCKED,1); transition(PBL_THREAD_RUNNING,64000);
 assert(s_written==before);
 // Counter wrap and bounded overwrite have explicit loss accounting.
 callback=(void *)0x1234; prv_observe();
 T5AI_AON_RTC_COUNT_LOW=UINT32_MAX-100; s_since=UINT32_MAX-100;
 transition(PBL_THREAD_READY,6400);
 assert(s_ring[(s_written-1)%RECORDS].span==6400);
 for(unsigned i=0;i<100;i++) transition(i%2 ? PBL_THREAD_READY:PBL_THREAD_RUNNING,6400);
 assert(s_written-s_read==RECORDS && s_lost>0);
 // Exercise actual selection accounting, including a deliberately wrong lower-priority pick.
 bg.backend.state=PBL_THREAD_READY;
 for(unsigned prio=0;prio<4;++prio) {
  app.prio=prio;
  __cyg_profile_func_enter((void *)sched_switch_in,0);pbl_cur=&app;
  __cyg_profile_func_exit((void *)sched_switch_in,0);
 }
 assert(s_pick_lower==1 && s_pick_equal==1 && s_pick_higher==2);
 __cyg_profile_func_enter((void *)sched_switch_in,0);pbl_cur=&bg;
 bg.backend.state=PBL_THREAD_RUNNING;
 __cyg_profile_func_exit((void *)sched_switch_in,0);assert(s_pick_bg==1);
 prv_snapshot(); assert(logs>0 && !depth);
 __wrap_system_task_timer_init();
 // Non-BG callers pass through without altering BG operation metadata.
 pbl_cur=&app; unsigned written=s_written;
 __wrap_pbl_mutex_unlock(&mutex); assert(!s_op.kind && s_written>=written);
 return 0;
}
'''
class BGTraceTest(unittest.TestCase):
    def test_decode_ready_wait_and_cpu_evidence(self):
        from tools.unhygienix_bg_trace import summarize
        text = """T5BG start hz=32000
T5BG now=64000 cb=00000000 state=0 age_ms=2000 ready_ms=1000 blocked_ms=0 run_ms=0
T5BG waitq=00000000 op=1 object=28011234 caller=021456ab owner=0 owner_lr=00000000 lost=0
T5BG picks_bg=7 picks_higher=99 picks_equal=0 picks_lower=0
T5BG task=18 name=App <FdF Time> cpu_ticks=835 state=0 base=2 effective=2 waitq=00000000
T5BG segment=1 at=64000 ms=500 state=0 cb=021234ab waitq=00000000 base=1
T5BG segment=1 op=3 object=28011234 caller=021456ab owner=2 owner_lr=021567ab effective=1
"""
        report = summarize(text)
        self.assertEqual(report['max_observed_ready_age_ms'], 2000)
        self.assertEqual(report['windows'][0]['scheduler_picks'],
                         dict(picks_bg=7, picks_higher=99, picks_equal=0, picks_lower=0))
        self.assertEqual(report['completed_segments_ms'], {'ready': 500})
        self.assertEqual(report['segments'][0]['caller'], 0x21456ab)
        self.assertEqual(report['task_cpu_ticks_in_printed_windows'], {'18:App <FdF Time>': 835})
        self.assertEqual(report['windows'][0]['operation']['op'], 1)

    def test_actual_state_and_wait_accounting(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp)
            shared='''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define PBL_THREAD_NAME_LEN 16
#define PBL_PRIO_MAX 4
#define PBL_MSEC(x) ((pbl_timeout_t){x})
#define PBL_THREAD_STACK_DEFINE(n,s) static uint8_t n[s]
typedef struct {uint32_t ticks;} pbl_timeout_t;
struct pbl_waitq { void *head; };
struct pbl_thread {struct {struct pbl_thread *all_next; struct pbl_waitq *waitq;
 uint32_t run_time,switched_in_at,state,base_prio;} backend; uint32_t id,prio; char name[16];};
struct pbl_mutex {struct pbl_thread *owner; uint32_t count; uintptr_t lock_lr;
 struct {struct pbl_waitq waitq;} backend;};
struct pbl_thread_attr {char *name; void (*entry)(void *); unsigned prio;
 bool privileged; void *stack; size_t stack_size;};
enum {PBL_THREAD_READY,PBL_THREAD_RUNNING,PBL_THREAD_BLOCKED};
void pbl_irq_lock(void); void pbl_irq_unlock(void);
uint32_t pbl_uptime_ticks(void);
void *system_task_get_current_callback(void);
struct pbl_thread *pebble_task_get_thread(int);
#define PebbleTask_KernelBackground 1
int pbl_thread_create(struct pbl_thread *,const struct pbl_thread_attr *);
void pbl_thread_sleep(pbl_timeout_t);
#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 4
void probe_log(const char *,...);
#define PBL_LOG_DBG(...) probe_log(__VA_ARGS__)
#define PBL_ASSERTN(x) assert(x)
'''
            for name in ['pbl/kernel/irq.h','pbl/kernel/mutex.h','pbl/kernel/time.h','pbl/kernel/sched.h',
                         'pbl/kernel/thread.h','pbl/logging/logging.h','pbl/services/system_task.h',
                         'kernel/pebble_tasks.h','pbl/kernel/assert.h']:
                p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
            (d/'stub.h').write_text(shared)
            (d/'test.c').write_text(HARNESS)
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-no-pie',
                '-I',str(d),'-I',str(ROOT/'include'),
                f'-DPROFILE_SOURCE="{ROOT / "soc/t5ai/bg_trace.c"}"',str(d/'test.c'),
                '-o',str(d/'test')],check=True,capture_output=True)
            subprocess.run([str(d/'test')],check=True)
if __name__=='__main__': unittest.main()
