# SPDX-License-Identifier: Apache-2.0
"""Check the bounded board diagnostic's monotonic schedule and cancellation."""

import unittest

from tools.tests.native_t5ai_harness import run

PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int status_t;
#define nullptr NULL
#define PBL_MSEC(ms) (ms)
#define pbl_ms_to_ticks(ms) (ms)
struct pbl_work {void (*fn)(struct pbl_work *);};
struct pbl_delayed_work {struct pbl_work work;};
static int pbl_sys_workq_high;

#define TIMER_INVALID_ID 0
#define TIMER_START_FLAG_REPEATING 1
#define S_SUCCESS 0
#define E_AGAIN -1
#define E_ERROR -2
#define T5AI_PM_NATIVE_ALARM_MAX_TICKS 16000u
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_DBG(...) ((void)0)
static unsigned created,deleted,arms,cancels,gen;
static bool task,fire,reject,drop;
static void (*timer_cb)(struct pbl_work *);
static void (*queued)(void *);
static void pbl_delayed_work_init(struct pbl_delayed_work *t,void (*fn)(struct pbl_work *)){++created;t->work.fn=fn;}
static bool pbl_delayed_work_schedule(int *q,struct pbl_delayed_work *t,unsigned ms){assert(q==&pbl_sys_workq_high && ms==1000);timer_cb=t->work.fn;return true;}
static bool pbl_delayed_work_schedule_next(struct pbl_delayed_work *t,unsigned ms){assert(ms==1000);return true;}
static bool pbl_delayed_work_cancel(struct pbl_delayed_work *t){assert(task);++deleted;timer_cb=NULL;return true;}
static bool pbl_sys_work_try_call(void (*cb)(void *),void *arg){assert(!task && !arg);if(drop)return false;assert(!queued);queued=cb;return true;}
static int t5ai_native_alarm_init(void){assert(task);return 0;}
static bool t5ai_native_alarm_fired(unsigned id,uint32_t *tick){assert(task);*tick=17;return fire && id==1;}
static int t5ai_native_alarm_arm(unsigned ticks,uint32_t *id){assert(task && (ticks==3200 || ticks==16000));++arms;*id=++gen;return reject?-2:0;}
static int t5ai_native_alarm_cancel(unsigned id){assert(task && id==gen);++cancels;fire=false;return 0;}
static bool t5ai_native_alarm_healthy(void){assert(task);return !reject;}
"""
MAIN = r"""
static void advance(void){assert(timer_cb);timer_cb(NULL);if(queued){void (*cb)(void *)=queued;queued=NULL;task=true;cb(NULL);task=false;}}
int main(void){
 t5ai_native_alarm_test_start();t5ai_native_alarm_test_start();assert(created==1);
 drop=true;advance();assert(!s_step && !arms);drop=false;
#if CASE == 1
 reject=true;advance();assert(s_test_stopped && deleted==1 && arms==1 && !timer_cb);
#else
 advance();assert(s_step==1 && arms==1);fire=true;
 for(unsigned i=0;i<5;++i)advance();
 assert(s_step==6 && arms==2 && cancels==2 && s_test_stopped && deleted==1 && !timer_cb);
#endif
 t5ai_native_alarm_test_start();assert(created==1);task=true;prv_step(NULL);task=false;
 assert(deleted==1);return 0;
}
"""


class AlarmDiagnosticTest(unittest.TestCase):
    def test_timer_context_drop_retry_and_one_shot_lifetime(self):
        for case in range(2):
            with self.subTest(case=case):
                run(
                    "soc/t5ai/native_alarm_test.c",
                    PREFIX,
                    MAIN,
                    defines=(f"CASE={case}",),
                )


if __name__ == "__main__":
    unittest.main()
