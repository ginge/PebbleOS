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
typedef unsigned TimerID;
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
static void (*timer_cb)(void *),(*queued)(void *);
static unsigned new_timer_create(void){++created;return 9;}
static bool new_timer_start(unsigned id,unsigned ms,void (*cb)(void *),void *arg,unsigned flags){assert(id==9 && ms==1000 && !arg && flags==1);timer_cb=cb;return true;}
static void new_timer_delete(unsigned id){assert(task && id==9);++deleted;timer_cb=NULL;}
static bool system_task_add_callback_droppable(void (*cb)(void *),void *arg){assert(!task && !arg);if(drop)return false;assert(!queued);queued=cb;return true;}
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
