# SPDX-License-Identifier: Apache-2.0
"""Timer-registration diagnostics preserve the real API and never rearm timers."""

import re
import unittest

from tools.tests.native_t5ai_harness import ROOT, run


class DeadlineProfile(unittest.TestCase):
    def test_real_calls_failures_repeat_phase_and_candidates(self):
        header = (ROOT / "include/pbl/soc/t5ai/sleep_deadline_profile.h").read_text()
        header = re.sub(r"^#(?:include|pragma).*$", "", header, flags=re.MULTILINE)
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_TASK_TIMER_POOL_SIZE 16
#define TIMER_START_FLAG_REPEATING 1u
#define RTC_TICKS_HZ 1000u
typedef uint32_t TimerID;
typedef uint64_t RtcTicks;
typedef void (*NewTimerCallback)(void *);
static RtcTicks now;
static unsigned locks,starts,stops,deletes;
static bool result=true;
static RtcTicks t5ai_rtc_get_ticks(void){return now;}
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){assert(locks);--locks;}
static bool __real_new_timer_start(TimerID id,uint32_t ms,NewTimerCallback cb,void *ctx,uint32_t flags){
 assert(!locks && id && cb);++starts;return result;
}
static bool __real_new_timer_stop(TimerID id){assert(!locks && id);++stops;return result;}
static void __real_new_timer_delete(TimerID id){assert(!locks && id);++deletes;}
static void cb(void *ctx){assert(false);}
"""
        main = r"""
int main(void){
 T5AISleepTimerCandidate out[T5AI_SLEEP_PROFILE_CANDIDATES];
 memset(s_records,0xa5,sizeof(s_records));
 assert(__wrap_new_timer_start(1,100,cb,(void *)16,TIMER_START_FLAG_REPEATING));
 assert(starts==1 && s_records[0].id==0xa5a5a5a5u);t5ai_sleep_profile_timers_ready();assert(s_storage_ready);now=60;
 assert(t5ai_sleep_profile_timer_candidates(out)==1 && out[0].remaining_ms==40);
 assert(out[0].callback==(uintptr_t)cb);
 t5ai_sleep_profile_timers_ready();assert(t5ai_sleep_profile_timer_candidates(out)==1);
 assert(__wrap_new_timer_start(2,20,cb,NULL,0));
 assert(t5ai_sleep_profile_timer_candidates(out)==2 && out[0].id==2 && out[1].id==1);
 result=false;assert(!__wrap_new_timer_start(1,1,cb,NULL,0));
 now=81;assert(t5ai_sleep_profile_timer_candidates(out)==1 && out[0].remaining_ms==19);
 assert(out[0].period_ms==100 && starts==3);
 assert(!__wrap_new_timer_stop(1) && stops==1);
 assert(!t5ai_sleep_profile_timer_candidates(out));
 result=true;assert(__wrap_new_timer_start(1,0,cb,NULL,1));
 assert(t5ai_sleep_profile_timer_candidates(out)==1 && out[0].period_ms==1);
 __wrap_new_timer_delete(1);assert(deletes==1 && !t5ai_sleep_profile_timer_candidates(out));
 for(unsigned id=1;id<=10;++id)assert(__wrap_new_timer_start(id,id,cb,NULL,0));
 assert(t5ai_sleep_profile_timer_candidates(out)==T5AI_SLEEP_PROFILE_CANDIDATES);
 for(unsigned i=0;i<T5AI_SLEEP_PROFILE_CANDIDATES;++i)assert(out[i].id==i+1 && out[i].remaining_ms==i+1);
 assert(starts==14 && stops==1 && deletes==1 && !locks);
 now=UINT32_MAX-4u;assert(__wrap_new_timer_start(15,10,cb,NULL,0));now+=8;
 assert(t5ai_sleep_profile_timer_candidates(out)==1 && out[0].id==15 && out[0].remaining_ms==2);
 assert(__wrap_new_timer_start(15,UINT32_MAX,cb,NULL,0));
 assert(!t5ai_sleep_profile_timer_candidates(out) && t5ai_sleep_profile_timers_untracked()==1);
 assert(starts==16 && !locks);return 0;
}
"""
        run("soc/t5ai/sleep_deadline_profile.c", prefix + header, main)
