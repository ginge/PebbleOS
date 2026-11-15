# SPDX-License-Identifier: Apache-2.0
"""Exercise the migrated idle-state callback after CP coordination queues work."""

import unittest

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_native_objects import body


class IdleTest(unittest.TestCase):
    def test_scheduler_recheck_prevents_sleep_after_queuing_work(self):
        run(
            "soc/t5ai/idle.c",
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static bool confirm=true,queued,deep;
static unsigned services,checks,enters,wfi;
static uint32_t t5ai_tick_idle_deadline(uint32_t max){assert(max==UINT32_MAX);return 123;}
static void t5ai_sleep_service(uint32_t deadline){assert(deadline==123);++services;if(queued)confirm=false;}
static bool sched_idle_confirm(void){++checks;return confirm;}
static bool t5ai_sleep_enter(uint32_t deadline){assert(confirm && deadline==123);++enters;return deep;}
static void pbl_cpu_idle(void){assert(confirm);++wfi;}
""",
            r"""
int main(void){
 queued=true;prv_idle();assert(services==1 && checks==1 && !enters && !wfi);
 queued=false;confirm=true;deep=true;prv_idle();assert(services==2 && checks==2 && enters==1 && !wfi);
 deep=false;prv_idle();assert(services==3 && checks==3 && enters==2 && wfi==1);
 return 0;
}
""",
            defines=("CONFIG_T5AI_RETAINED_SLEEP",),
            transform=lambda source: body(source, "prv_idle"),
        )
