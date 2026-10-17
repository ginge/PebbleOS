# SPDX-License-Identifier: Apache-2.0
"""Exercise actual scheduler probe code with controlled scheduler selections."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 0
#define PBL_LOG_DBG(...) ((void)0)
#define PBL_THREAD_NAME_LEN 16
#define TIMER_START_FLAG_REPEATING 1
#define PebbleTask_KernelBackground 0
#define PebbleTask_App 1
#define NumPebbleTask 2
#define PBL_THREAD_READY 0
#define PBL_THREAD_RUNNING 1
#define PBL_THREAD_BLOCKED 2
#define PBL_THREAD_DEAD 4
extern uint32_t rtc;
#define T5AI_AON_RTC_COUNT_LOW rtc
struct pbl_thread {struct {uint32_t state,run_time,switched_in_at;} backend;uint32_t id,prio;char name[16];};
void pbl_irq_lock(void);void pbl_irq_unlock(void);uint32_t pbl_uptime_ticks(void);
void *system_task_get_current_callback(void);uint32_t new_timer_create(void);
bool new_timer_start(uint32_t,uint32_t,void(*)(void*),void*,uint32_t);
"""
HARNESS = r"""#include <assert.h>
#include SOURCE
uint32_t rtc;
struct pbl_thread bg={.id=1,.prio=1},app={.id=2,.prio=2},other={.prio=3};
struct pbl_thread *g_task_threads[2]={&bg,&app},*pbl_cur,*selected;
struct pbl_thread *__real_sched_switch_in(void){pbl_cur=selected;selected->backend.state=1;return selected;}
void __real_system_task_timer_init(void){}
void pbl_irq_lock(void){}void pbl_irq_unlock(void){}
uint32_t pbl_uptime_ticks(void){return rtc/32;}
void *system_task_get_current_callback(void){return 0;}
uint32_t new_timer_create(void){return 1;}
bool new_timer_start(uint32_t a,uint32_t b,void(*c)(void*),void*d,uint32_t e){return true;}
static void pick(struct pbl_thread *t,uint32_t time){bg.backend.state=0;selected=t;rtc=time;assert(__wrap_sched_switch_in()==t);}
int main(void){
 pick(&app,10);assert(!s_trace.picks);
 __wrap_system_task_timer_init();
 pick(&app,100);pick(&app,200);assert(s_trace.app_higher==2&&s_trace.ready_peak==100);
 pick(&other,300);assert(s_trace.other_higher==1);
 other.prio=1;pick(&other,400);assert(s_trace.equal==1);
 other.prio=0;pick(&other,500);assert(s_trace.lower==1);
 pick(&bg,600);assert(s_trace.bg==1&&!s_trace.ready&&s_trace.ready_peak==500);
 bg.backend.state=2;selected=&app;rtc=700;__wrap_sched_switch_in();assert(s_trace.app_higher==2);
 rtc=800;prv_report(0);assert(s_trace.app_id==2);
 app.id=3;app.backend.run_time=0;rtc=900;prv_report(0);assert(s_trace.app_id==3);
 assert(sizeof(s_trace)<=64);
 return 0;
}
"""


class BgPickTraceTest(unittest.TestCase):
    def test_selector_and_peak_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for header in (
                "pbl/kernel/time.h", "pbl/kernel/sched.h", "pbl/kernel/irq.h", "pbl/kernel/thread.h",
                "pbl/logging/logging.h", "pbl/services/new_timer/new_timer.h",
                "pbl/services/system_task.h", "pbl/soc/t5ai/rtc.h",
            ):
                path = directory / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            (directory / "stub.h").write_text(STUB)
            (directory / "test.c").write_text(HARNESS)
            executable = directory / "test"
            subprocess.run([
                "cc", "-std=c11", "-I" + str(directory),
                '-DSOURCE="' + str(ROOT / "soc/t5ai/bg_pick_trace.c") + '"',
                str(directory / "test.c"), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
