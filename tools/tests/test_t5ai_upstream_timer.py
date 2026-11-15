# SPDX-License-Identifier: Apache-2.0
"""Clock-event migration: pending IRQs, AON wrap, and clock changes across sleep."""

import unittest
from pathlib import Path
from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r"""
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_T5AI_RETAINED_SLEEP 1
#define nullptr NULL
#define T5AI_AON_RTC_HZ 32000u
static uint32_t aon, SystemCoreClock = 240000000u, resume_hz = 240000000u;
#define T5AI_AON_RTC_COUNT_LOW aon
struct pbl_device {int (*init)(const struct pbl_device *);};
struct pbl_clocksource;
struct pbl_clockevent;
struct pbl_clocksource_ops {uint32_t (*read)(const struct pbl_clocksource *);};
struct pbl_clockevent_ops {
 void (*set_next)(const struct pbl_clockevent *, uint32_t);
 void (*shutdown)(const struct pbl_clockevent *);
};
struct pbl_clocksource {struct pbl_device dev; const struct pbl_clocksource_ops *ops; uint32_t mask;};
struct pbl_clockevent {struct pbl_device dev; const struct pbl_clockevent_ops *ops; uint32_t min_delta,max_delta;};
#define PBL_DEVICE_STATE_DEFINE(sym)
#define PBL_CLOCKSOURCE_INIT(sym,n,fn,o,bits) {.dev={fn},.ops=o,.mask=UINT32_MAX}
#define PBL_CLOCKEVENT_INIT(sym,n,fn,o,lo,hi) {.dev={fn},.ops=o,.min_delta=lo,.max_delta=hi}
static struct {uint32_t CTRL,LOAD,VAL;} systick;
static struct {uint32_t ICSR;} scb;
#define SysTick (&systick)
#define SCB (&scb)
#define SysTick_CTRL_ENABLE_Msk 1u
#define SysTick_CTRL_TICKINT_Msk 2u
#define SysTick_CTRL_CLKSOURCE_Msk 4u
#define SCB_ICSR_PENDSTCLR_Msk (1u<<25)
#define SCB_ICSR_PENDSTSET_Msk (1u<<26)
#define SCB_ICSR_VECTPENDING_Msk (0x1ffu<<12)
#define SysTick_IRQn -1
#define PBL_IRQ_PRIO_KERNEL 255
#define __DSB() ((void)0)
static uint32_t source_hz,event_hz,events;
static void pbl_clocksource_set_hz(const struct pbl_clocksource *s,uint32_t hz){source_hz=hz;}
static void pbl_clockevent_set_hz(const struct pbl_clockevent *e,uint32_t hz){event_hz=hz;}
static void pbl_clockevent_handler(const struct pbl_clockevent *e){assert(!SysTick->CTRL);++events;}
static void t5ai_power_update_clock(void){}
static void t5ai_power_sync_tick_clock(void){SystemCoreClock=resume_hz;}
static void NVIC_SetPriority(int irq,uint32_t priority){assert(irq==-1 && priority==255);}
"""


class UpstreamTimerTest(unittest.TestCase):
    def test_deadlines_pending_interrupts_wrap_and_clock_restore(self):
        source = (ROOT / "soc/t5ai/tick.c").read_text()
        headers = {
            name: ""
            for name in (
                "pbl/drivers/timer.h",
                "pbl/kernel/irq.h",
                "pbl/soc/t5ai/power.h",
                "pbl/soc/t5ai/rtc.h",
                "pbl/soc/t5ai/tick.h",
                "cmsis_core.h",
            )
        }
        run_c(
            PREFIX
            + source
            + r"""
int main(void){
 assert(prv_source_init(NULL)==0 && source_hz==32000);
 assert(prv_event_init(NULL)==0 && event_hz==240000000);
 aon=UINT32_MAX-15u;prv_set_next(&s_systick,240000u);
 assert(SysTick->LOAD==239999u && SysTick->CTRL==7 && s_deadline==16u);
 SCB->ICSR=SCB_ICSR_PENDSTSET_Msk;
 assert(!t5ai_tick_prepare_retained() && SysTick->CTRL==7 && !s_saved_ctrl);
 SCB->ICSR=1u<<12;
 assert(!t5ai_tick_prepare_retained() && SysTick->CTRL==7);
 SCB->ICSR=0;
 assert(t5ai_tick_prepare_retained() && !SysTick->CTRL);
 assert(!t5ai_tick_prepare_retained());
 aon=0;resume_hz=120000000u;t5ai_tick_resume_idle();
 assert(!s_saved_ctrl && SystemCoreClock==120000000u && SysTick->LOAD==59999u);
 assert(s_deadline==16u && SysTick->CTRL==7);
 SCB->ICSR=0;assert(t5ai_tick_prepare_retained());
 aon=17;t5ai_tick_resume_idle();assert(SysTick->LOAD==63u && SysTick->CTRL==7);
 SysTick_Handler();assert(events==1 && !SysTick->CTRL);
 assert(!t5ai_tick_prepare_retained());
 aon=0xabcdef01;assert(prv_read(&s_aon)==aon);
 return 0;
}
""",
            headers,
        )
