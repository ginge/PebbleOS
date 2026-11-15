# SPDX-License-Identifier: Apache-2.0
"""Exercise native timer observation without extending timer lifetimes."""

import unittest

from tools.tests.native_t5ai_harness import run

PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define nullptr NULL
typedef uint32_t pbl_tick_t;
typedef struct {pbl_tick_t ticks;} pbl_timeout_t;
struct pbl_timer {void (*fn)(struct pbl_timer *);uint64_t expires;};
#define T5AI_SLEEP_PROFILE_CANDIDATES 8u
typedef struct {uint32_t id,remaining_ms,period_ms;uintptr_t callback,context;} T5AISleepTimerCandidate;
static uint64_t now;
static unsigned locks,starts,stops;
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){assert(locks);--locks;}
static uint64_t pbl_uptime_ticks(void){return now;}
static uint32_t pbl_ticks_to_ms(uint64_t ticks){return ticks;}
static pbl_tick_t pbl_timer_remaining(struct pbl_timer *t){return t->expires>now?t->expires-now:0;}
static void __real_pbl_timer_start(struct pbl_timer *t,pbl_timeout_t d){assert(locks);++starts;t->expires=now+(d.ticks?d.ticks:1);}
static void __real_pbl_timer_stop(struct pbl_timer *t){assert(locks);++stops;t->expires=0;}
static void expired(struct pbl_timer *t){}
"""
MAIN = r"""
int main(void){
 struct pbl_timer timers[65];memset(timers,0,sizeof(timers));
 for(unsigned i=0;i<9;++i){timers[i].fn=expired;__wrap_pbl_timer_start(&timers[i],(pbl_timeout_t){100-i});}
 assert(t5ai_sleep_profile_timers_untracked()==1);
 t5ai_sleep_profile_timers_ready();t5ai_sleep_profile_timers_ready();
 T5AISleepTimerCandidate out[8];pbl_irq_lock();unsigned n=t5ai_sleep_profile_timer_candidates(out);pbl_irq_unlock();
 assert(n==8 && out[0].remaining_ms==93 && out[7].remaining_ms==100);
 assert(out[0].callback==(uintptr_t)expired && out[0].context==(uintptr_t)&timers[7]);
 unsigned before=starts;__wrap_pbl_timer_stop(&timers[7]);
 pbl_irq_lock();n=t5ai_sleep_profile_timer_candidates(out);pbl_irq_unlock();assert(n==7 && starts==before && stops==1);
 now=100;for(unsigned i=0;i<65;++i)__wrap_pbl_timer_start(&timers[i],(pbl_timeout_t){200+i});
 assert(t5ai_sleep_profile_timers_untracked()==2);
 now=1000;pbl_irq_lock();assert(!t5ai_sleep_profile_timer_candidates(out));pbl_irq_unlock();
 // An expired stack/heap timer must never be dereferenced by the observer.
 struct pbl_timer *released=malloc(sizeof(*released));released->fn=expired;
 __wrap_pbl_timer_start(released,(pbl_timeout_t){1});++now;free(released);
 pbl_irq_lock();assert(!t5ai_sleep_profile_timer_candidates(out));pbl_irq_unlock();
 now=UINT32_MAX-10ULL;timers[0].fn=expired;
 __wrap_pbl_timer_start(&timers[0],(pbl_timeout_t){30});now+=20;
 pbl_irq_lock();assert(t5ai_sleep_profile_timer_candidates(out)==1 && out[0].remaining_ms==10);pbl_irq_unlock();
 __wrap_pbl_timer_start(&timers[0],(pbl_timeout_t){50});
 pbl_irq_lock();assert(t5ai_sleep_profile_timer_candidates(out)==1 && out[0].remaining_ms==50);pbl_irq_unlock();
 __wrap_pbl_timer_stop(&timers[0]);assert(!locks);
 return 0;
}
"""


class NativeDeadlineTimerTest(unittest.TestCase):
    def test_start_stop_rearm_expiry_capacity_and_freed_timer(self):
        run("soc/t5ai/sleep_deadline_profile.c", PREFIX, MAIN)
