# SPDX-License-Identifier: Apache-2.0
"""Check actual watchdog diagnostic wrappers, delegation and bounded records."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tests.test_t5ai_bg_pick_trace import STUB as BASE_STUB

ROOT = Path(__file__).resolve().parents[2]
STUB = BASE_STUB + "\ntypedef void *(*pbl_task_wdt_callback_t)(int,void*);\n"
HARNESS = r"""
#include <assert.h>
#include SOURCE
uint32_t rtc;
struct pbl_thread bg={.id=7}, other={.id=8};
struct pbl_thread *pbl_cur=&bg;
struct pbl_thread *g_task_threads[2]={&bg,&other};
static unsigned suspended,resumed,fed_all,fed_channel,fed_thread;
void pbl_irq_lock(void) {} void pbl_irq_unlock(void) {}
void *system_task_get_current_callback(void) {return (void*)0x1234;}
void __real_pbl_task_wdt_suspend(uint32_t timeout) {
 assert(s_written);assert(s_ring[(s_written-1)%RECORDS].arg==timeout);++suspended;
}
void __real_pbl_task_wdt_resume(void) {
 assert(s_ring[(s_written-1)%RECORDS].kind==Resume);++resumed;
}
void __real_pbl_task_wdt_feed_all(void) {assert(s_feeds[0].count);++fed_all;}
int __real_pbl_task_wdt_add(struct pbl_thread*t,uint32_t m,pbl_task_wdt_callback_t cb,void*d) {return 2;}
int __real_pbl_task_wdt_feed(int channel) {++fed_channel;return -12;}
void __real_pbl_task_wdt_feed_thread(struct pbl_thread*t) {++fed_thread;}
int main(void) {
 rtc=100;__wrap_pbl_task_wdt_suspend(60000);
 assert(s_ring[0].kind==Suspend&&s_ring[0].rtc==100&&s_ring[0].thread==7);
 assert(s_ring[0].callback==0x1234);
 rtc=200;__wrap_pbl_task_wdt_resume();assert(s_written==2);
 __wrap_pbl_task_wdt_feed_all();assert(s_feeds[0].count==1);
 __wrap_pbl_task_wdt_add(&other,5000,0,0);assert(s_bg_channel==-1);
 __wrap_pbl_task_wdt_add(0,5000,0,0);assert(s_bg_channel==2);
 assert(__wrap_pbl_task_wdt_feed(1)==-12);assert(s_feeds[1].count==0);
 assert(__wrap_pbl_task_wdt_feed(2)==-12);assert(s_feeds[1].count==1);
 __wrap_pbl_task_wdt_feed_thread(&other);assert(!s_feeds[2].count);
 __wrap_pbl_task_wdt_feed_thread(&bg);assert(s_feeds[2].count==1);
 assert(s_feeds[2].last.arg==7);
 for(unsigned i=0;i<10;++i){rtc++;__wrap_pbl_task_wdt_suspend(i);}
 assert(s_written==12&&s_read==4&&s_lost==4);
 t5ai_wdt_cause_trace_dump();assert(s_read==8);
 assert(s_feeds[0].reported==1&&s_feeds[1].reported==1&&s_feeds[2].reported==1);
 t5ai_wdt_cause_trace_dump();assert(s_read==12&&s_lost==4);
 assert(suspended==11&&resumed==1&&fed_all==1&&fed_channel==2&&fed_thread==2);
 assert(sizeof(s_ring)+sizeof(s_feeds)+sizeof(s_written)*3+sizeof(s_bg_channel)<=400);
 return 0;
}
"""


class WdtCauseTraceTest(unittest.TestCase):
    def test_actual_wrappers_and_bounded_storage(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for header in (
                "pbl/kernel/irq.h", "pbl/kernel/thread.h", "pbl/logging/logging.h",
                "pbl/services/system_task.h", "pbl/soc/t5ai/rtc.h", "pbl/task_wdt/task_wdt.h",
            ):
                path = directory / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            (directory / "stub.h").write_text(STUB)
            (directory / "test.c").write_text(HARNESS)
            executable = directory / "test"
            subprocess.run([
                "cc", "-std=c11", "-I" + str(directory),
                '-DSOURCE="' + str(ROOT / "soc/t5ai/wdt_cause_trace.c") + '"',
                str(directory / "test.c"), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
