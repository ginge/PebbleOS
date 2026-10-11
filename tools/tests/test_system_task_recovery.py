# SPDX-License-Identifier: Apache-2.0
"""Run actual system-task recovery helpers against delayed BG progress."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define PBL_THREAD_NAME_LEN 16
#define APP_TASK_PRIORITY 2
#define PBL_PRIO_IDLE 0
#define APP_THROTTLE_TIME_MS 300
#define PebbleTask_App 1
#define PBL_LOG_DBG(...)
#define PBL_LOG_WRN(...)
struct pbl_thread {uint32_t id;unsigned priority;};
static struct pbl_thread app={.id=17,.priority=2};
static int s_throttle_timer=1,s_wdt_channel=2;
static uint32_t s_watchdog_feed_count,s_throttle_feed_count,s_throttled_app_id;
static unsigned timers,feeds;
static int locked;
static void pbl_irq_lock(void){++locked;}
static void pbl_irq_unlock(void){assert(locked>0);--locked;}
static int feed_result;
static struct pbl_thread *pebble_task_get_thread(int t){return &app;}
static const char *pbl_thread_name(struct pbl_thread *t){return "FdF Time";}
static void pbl_thread_prio_set(struct pbl_thread *t,unsigned p){t->priority=p;}
static bool new_timer_start(int t,unsigned ms,void(*cb)(void*),void*d,unsigned flags){
 assert(t==1&&ms==300);++timers;return true;
}
static int pbl_task_wdt_feed(int channel){assert(channel==2);++feeds;return feed_result;}
HELPERS
int main(void){
 prv_app_throttle_start();assert(app.priority==0&&timers==1);
 for(int i=0;i<3;i++){prv_app_throttle_end(0);assert(app.priority==0);}
 assert(timers==4);
 system_task_watchdog_feed();assert(feeds==1);
 prv_app_throttle_end(0);assert(app.priority==2&&timers==4);
 prv_app_throttle_start();prv_app_throttle_start();
 prv_app_throttle_end(0);assert(app.priority==0);
 feed_result=-1;system_task_watchdog_feed();
 prv_app_throttle_end(0);assert(app.priority==0);
 feed_result=0;system_task_watchdog_feed();
 prv_app_throttle_end(0);assert(app.priority==2);
 prv_app_throttle_start();app.id=18;app.priority=2;
 unsigned before=timers;prv_app_throttle_end(0);
 assert(app.priority==2&&timers==before);assert(!locked);
}
"""


class SystemTaskRecoveryTest(unittest.TestCase):
    def test_recovery_waits_for_successful_bg_feed(self):
        source = (ROOT / "fw/services/system_task/service.c").read_text()
        start = source.index("static void prv_app_throttle_end(void *data)")
        end = source.index("//! The system task is starved", start)
        feed_start = source.index("void system_task_watchdog_feed(void)")
        feed_end = source.index("\nstatic void handle_system_task_send_failure", feed_start)
        helpers = source[start:end] + source[feed_start:feed_end]
        with tempfile.TemporaryDirectory() as tmp:
            cfile = Path(tmp) / "test.c"
            exe = Path(tmp) / "test"
            cfile.write_text(HARNESS.replace("HELPERS", helpers))
            subprocess.run(["cc", "-std=c11", str(cfile), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
