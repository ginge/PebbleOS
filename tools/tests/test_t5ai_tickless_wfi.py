# SPDX-License-Identifier: Apache-2.0
"""Exercise the production core-timer contract, idle path and CP vote guards."""

import re
import unittest

from tools.tests.native_t5ai_harness import ROOT, run
from tools.tests.test_t5ai_native_objects import body

PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define PBL_TICK_HZ 1000u
#define CONFIG_KERNEL_TICK_HZ 1000u
#define T5AI_AON_RTC_HZ 32000u
static uint32_t aon, hw_mode, hw_cpu1, SystemCoreClock, cp_bound;
#define T5AI_AON_RTC_COUNT_LOW aon
#define S_SUCCESS 0
#define T5AICpuFrequency_240MHz 4
static int t5ai_cp_mailbox_vote_cpu1_frequency(unsigned f){assert(f==4);return 0;}
static uint32_t t5ai_cp_mailbox_wfi_clock_min_hz(void){return cp_bound;}
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_DBG(...) ((void)0)
#define PBL_LOG_ERR(...) ((void)0)
#define PROFILER_INIT
#define PROFILER_START
typedef int status_t;
static struct {volatile uint32_t CTRL,LOAD,VAL;} systick;
static struct {volatile uint32_t ICSR,SCR;} scb;
#define SysTick (&systick)
#define SCB (&scb)
#define SysTick_CTRL_ENABLE_Msk 1u
#define SysTick_CTRL_TICKINT_Msk 2u
#define SysTick_CTRL_CLKSOURCE_Msk 4u
#define SCB_SCR_SLEEPDEEP_Msk 4u
#define SCB_ICSR_PENDSTSET_Msk (1u<<26)
#define SCB_ICSR_VECTPENDING_Msk (0x1ffu<<12)
static bool masked, race, allowed=true, confirmed=true;
static unsigned ticks,tick_calls,catchups,wfis,confirms,pend_requests;
static unsigned wake_ticks;
static bool wake_timer;
static uint32_t programmed;
static void __disable_irq(void){assert(!masked);masked=true;}
static void __enable_irq(void){assert(masked);masked=false;}
static void __DSB(void){
 assert(masked);
 if(race && SysTick->CTRL==0){race=false;aon+=32;SCB->ICSR|=SCB_ICSR_PENDSTSET_Msk;}
}
static void __ISB(void){assert(masked);}
static void pend_systick(void){++pend_requests;SCB->ICSR|=SCB_ICSR_PENDSTSET_Msk;}
void pbl_kernel_tick_isr(void){assert(masked);++ticks;++tick_calls;}
void pbl_idle_slept(unsigned n){assert(masked);ticks+=n;++catchups;}
static void __WFI(void);
typedef uint64_t RtcTicks;
typedef uint32_t pbl_tick_t;
static RtcTicks s_wfi_ticks;
static bool idle_is_allowed(void){return allowed;}
static bool pbl_idle_confirm(void){assert(masked);++confirms;return confirmed;}
static RtcTicks rtc_get_ticks(void){return aon/32;}
static uint32_t t5ai_interrupt_profile_irq_enter(void){return 0;}
static void t5ai_interrupt_profile_irq_exit(unsigned id,uint32_t start){assert(masked && id==3 && !start);}
"""

MAIN = r"""
static void __WFI(void){
 assert(masked && confirmed && allowed);++wfis;programmed=SysTick->LOAD+1;
 aon+=wake_ticks;
 if(wake_timer)SCB->ICSR|=SCB_ICSR_PENDSTSET_Msk;
}
static void service(void){
 assert(masked);SCB->ICSR&=~SCB_ICSR_PENDSTSET_Msk;SysTick_Handler();
}
static void reset(void){
 masked=true;aon=0;hw_mode=0x31;hw_cpu1=16;cp_bound=240000000;
 SystemCoreClock=240000000;SysTick->CTRL=7;SysTick->LOAD=239999;SysTick->VAL=123;
 SCB->ICSR=SCB->SCR=0;s_idle_ctrl=0;race=false;ticks=tick_calls=catchups=pend_requests=0;
 t5ai_tick_profile=(T5AITickProfile){0};t5ai_tick_start();
}
int main(void){
 reset();assert(t5ai_tick_prepare_idle(UINT32_MAX));
 assert(SysTick->LOAD<0x1000000 && SysTick->LOAD+1==2236u*7500u);
 aon=2236;SCB->ICSR=SCB_ICSR_PENDSTSET_Msk;t5ai_tick_resume_idle();
 assert(SysTick->LOAD==239999 && SysTick->CTRL==7 && SysTick->VAL==0);
 service();assert(ticks==69 && tick_calls==1 && catchups==1 && s_accounted==2208);
 service();assert(ticks==69 && tick_calls==1); // Coalesced pending IRQ is not counted twice.
 aon+=4;service();assert(ticks==70 && tick_calls==2);
 assert(t5ai_tick_profile.irqs==3 && t5ai_tick_profile.ticks==70 && t5ai_tick_profile.maximum==69);
 assert(t5ai_tick_profile.extended==1 && !t5ai_tick_profile.early);
 // External wake before timer expiry must pend the normal ISR and preserve fractions.
 reset();assert(t5ai_tick_prepare_idle(5) && SysTick->LOAD+1==128u*7500u);
 aon=48;t5ai_tick_resume_idle();assert(SCB->ICSR&SCB_ICSR_PENDSTSET_Msk);
 service();assert(ticks==1 && s_accounted==32 && t5ai_tick_profile.early==1);
 assert(t5ai_tick_prepare_idle(5) && SysTick->LOAD+1==112u*7500u);
 aon=64;t5ai_tick_resume_idle();service();assert(ticks==2);
 // Repeated sub-tick external wakes cannot discard accumulated AON fractions.
 reset();
 for(unsigned i=0;i<10;++i){assert(t5ai_tick_prepare_idle(10));aon+=8;t5ai_tick_resume_idle();
  if(SCB->ICSR&SCB_ICSR_PENDSTSET_Msk)service();}
 assert(ticks==2 && s_accounted==64);
 // Pending tick or external interrupt rejects extension without destroying the pending state.
 reset();SCB->ICSR=SCB_ICSR_PENDSTSET_Msk;assert(!t5ai_tick_prepare_idle(10));
 assert(SysTick->LOAD==239999 && SysTick->VAL==123 && SCB->ICSR==SCB_ICSR_PENDSTSET_Msk);
 SCB->ICSR=7u<<12;assert(!t5ai_tick_prepare_idle(10));assert(SCB->ICSR==7u<<12);
 // Invalid/failed CP vote, insufficient elected clock, DCO or timer configuration: fail closed.
 reset();cp_bound=0;assert(!t5ai_tick_prepare_idle(10));
 cp_bound=240000000;hw_mode=0x33;assert(!t5ai_tick_prepare_idle(10));
 hw_mode=0x10;assert(!t5ai_tick_prepare_idle(10));hw_mode=0x31;
 SCB->SCR=4;assert(!t5ai_tick_prepare_idle(10));SCB->SCR=0;
 SysTick->CTRL=3;assert(!t5ai_tick_prepare_idle(10));SysTick->CTRL=7;
 assert(!t5ai_tick_prepare_idle(0) && !t5ai_tick_prepare_idle(1) && !t5ai_tick_prepare_idle(2));
 // A periodic underflow racing with CTRL=0 is retained and restored, never cleared.
 reset();race=true;assert(!t5ai_tick_prepare_idle(10));
 assert(SysTick->CTRL==7 && SysTick->LOAD==239999 && (SCB->ICSR&SCB_ICSR_PENDSTSET_Msk));
 service();assert(ticks==1 && t5ai_tick_profile.aborted==1);
 // AON wrap and an already-due tick both retain exact once-only accounting.
 reset();aon=UINT32_MAX-31;t5ai_tick_start();aon+=16;
 assert(t5ai_tick_prepare_idle(5));aon+=64;t5ai_tick_resume_idle();service();
 assert(ticks==2 && aon-s_accounted==16);
 aon+=16;assert(!t5ai_tick_prepare_idle(5));service();assert(ticks==3);
 // Program against the acknowledged minimum, not a transient higher CP-elected rate.
 reset();hw_mode=0x30;SystemCoreClock=480000000;SysTick->LOAD=479999;
 assert(t5ai_tick_prepare_idle(10) && SysTick->LOAD+1==9u*32u*7500u);
 aon=144;hw_mode=0x31;SCB->ICSR=SCB_ICSR_PENDSTSET_Msk;t5ai_tick_resume_idle();
 assert(SystemCoreClock==240000000 && SysTick->LOAD==239999);service();assert(ticks==4);
 // A due AON tick alone must not add an IRQ ahead of the running periodic tick.
 reset();aon=32;assert(!t5ai_tick_prepare_idle(5));
 assert(!pend_requests && !SCB->ICSR && SysTick->VAL==123 && SysTick->LOAD==239999);
 service();assert(ticks==1);
 // A short fractional gap at the conservative floor must not shorten 1 ms.
 reset();cp_bound=160000000;aon=31;assert(!t5ai_tick_prepare_idle(3));
 assert(SysTick->LOAD==239999 && SysTick->VAL==123);
 // The pinned CP 240/320 transition passes through 160 MHz. Bound the wake
 // timer by that transient floor even when the elected steady clock is 240.
 reset();cp_bound=160000000;assert(t5ai_tick_prepare_idle(UINT32_MAX));
 assert(SysTick->LOAD+1==3355u*5000u);
 hw_mode=0x21;aon=320;SCB->ICSR=SCB_ICSR_PENDSTSET_Msk;t5ai_tick_resume_idle();
 assert(SystemCoreClock==160000000 && SysTick->LOAD==159999);service();assert(ticks==10);
 // Execute the actual idle path: idle gate, confirmation, external and timed wake.
 reset();masked=false;allowed=false;wfis=confirms=0;pbl_soc_idle(100);assert(!wfis && !confirms);
 allowed=true;confirmed=false;pbl_soc_idle(100);assert(!wfis && confirms==1);
 confirmed=true;wake_ticks=2236;wake_timer=true;pbl_soc_idle(100);
 assert(wfis==1 && programmed==2236u*7500u && !masked && !s_idle_ctrl);
 masked=true;service();assert(ticks==69);masked=false;
 wake_ticks=32;wake_timer=false;pbl_soc_idle(10);assert(wfis==2 && !masked);
 masked=true;service();assert(ticks==70);
 return 0;
}
"""

PM_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CP_PM_RX_CHANNEL 0x42
#define CP_PM_TX_CHANNEL 0x12
#define CP_RUNTIME_TIMEOUT_MS 5000
#define CP_PM_TIMEOUT_MS 100
#define CP_PM_SLEEP_COMMAND 3
#define CP_PM_SLEEP_APP_MODULE 12
#define CP_PM_CPU_FREQUENCY_COMMAND 4
#define CP_PM_CPU1_MODULE 38
#define T5AICpuFrequency_240MHz 4
#define T5AICpuFrequency_480MHz 6
#define S_SUCCESS 0
#define E_ERROR -1
#define E_AGAIN -2
#define E_INVALID_OPERATION -3
#define PBL_FOREVER 0
#define PBL_TICKS(n) (n)
#define FAILED(n) ((n)<0)
#define pbl_ms_to_ticks(n) (n)
typedef int status_t;
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef struct {uintptr_t address;} T5AICPMailboxFrame;
static struct {int ready;} s_pm_queue;
static int s_pm_mutex;
static bool s_pm_faulted,s_app_sleep_veto,s_wfi_clock_vote,owner,fail_send,timeout,fail_reply;
static T5AIMailboxMessage s_pm_message,response;
static unsigned in_flight_bound;
uint32_t t5ai_cp_mailbox_wfi_clock_min_hz(void);
static bool t5ai_mailbox_channel_is_owner(void){return owner;}
static status_t prv_ensure_runtime(void){return 0;}
static void pbl_mutex_lock(int *m,unsigned t){(void)t;assert(!*m);*m=1;}
static void pbl_mutex_unlock(int *m){assert(*m);*m=0;}
static int pbl_sem_take(int *s,unsigned t){(void)s;return !t || timeout?-1:0;}
static bool prv_dequeue(unsigned c,T5AICPMailboxFrame *f){assert(c==CP_PM_RX_CHANNEL);f->address=(uintptr_t)&response;return true;}
static int t5ai_mailbox_channel_send(unsigned c,const T5AIMailboxMessage *m,T5AIMailboxMessage *ack,unsigned t){
 (void)ack;assert(s_pm_mutex && c==CP_PM_TX_CHANNEL && t==5000);
 assert(t5ai_cp_mailbox_wfi_clock_min_hz()==in_flight_bound);
 response=(T5AIMailboxMessage){.header=m->header,.parameter1=fail_reply?1:0};
 return fail_send?-1:0;
}
"""
PM_MAIN = r"""
int main(void){
 assert(!t5ai_cp_mailbox_wfi_clock_min_hz());in_flight_bound=0;
 assert(!prv_pm_request(4,38,4) && !t5ai_cp_mailbox_wfi_clock_min_hz());
 assert(!prv_pm_request(3,12,0) && t5ai_cp_mailbox_wfi_clock_min_hz()==160000000);
 assert(!prv_pm_request(4,38,3) && !t5ai_cp_mailbox_wfi_clock_min_hz());
 assert(!prv_pm_request(4,38,5) && t5ai_cp_mailbox_wfi_clock_min_hz()==160000000);
 assert(!prv_pm_request(3,12,1) && !t5ai_cp_mailbox_wfi_clock_min_hz());
 fail_reply=true;assert(prv_pm_request(3,12,0)==E_ERROR && !t5ai_cp_mailbox_wfi_clock_min_hz());
 fail_reply=false;assert(!prv_pm_request(3,12,0));
 fail_send=true;assert(prv_pm_request(4,38,4)<0 && !t5ai_cp_mailbox_wfi_clock_min_hz());
 fail_send=false;assert(!prv_pm_request(4,38,4));
 // Unrelated peripheral requests retain the CPU contract until a timeout poisons PM.
 in_flight_bound=160000000;assert(!prv_pm_request(2,20,1));
 timeout=true;assert(prv_pm_request(2,20,1)==E_AGAIN && s_pm_faulted);
 assert(!t5ai_cp_mailbox_wfi_clock_min_hz());
 assert(prv_pm_request(3,12,0)==E_INVALID_OPERATION);
 return 0;
}
"""


class TicklessWFITest(unittest.TestCase):
    def test_deadline_pending_wrap_external_wake_and_clock_changes(self):
        power = (ROOT / "soc/t5ai/power.c").read_text()
        power = re.sub(r"^#include .*$", "", power, flags=re.MULTILINE)
        power = power.replace("*(volatile uint32_t *)0x44010020u", "hw_mode")
        power = power.replace("*(volatile uint32_t *)0x44010014u", "hw_cpu1")
        idle = body((ROOT / "soc/t5ai/idle.c").read_text(), "pbl_soc_idle")
        header = f'#include "{ROOT / "include/pbl/soc/t5ai/tick.h"}"\n'
        power_header = f'#include "{ROOT / "include/pbl/soc/t5ai/power.h"}"\n'
        run(
            "soc/t5ai/tick.c",
            PREFIX + header + power_header + power,
            idle + MAIN,
            defines=("CONFIG_T5AI_TICKLESS_WFI", "CONFIG_T5AI_TICK_PROFILE"),
            transform=lambda text: text.replace(
                "SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;", "pend_systick();"
            ),
        )

    def test_cp_contract_requires_success_and_invalidates_before_send(self):
        cp = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        code = body(cp, "t5ai_cp_mailbox_wfi_clock_min_hz") + body(cp, "prv_pm_request")
        run(
            "soc/t5ai/tick.c",
            PM_PREFIX,
            PM_MAIN,
            defines=("CONFIG_T5AI_TICKLESS_WFI",),
            transform=lambda _: code,
        )

    def test_sparse_profile_queue_failure_and_snapshot(self):
        header = f'#include "{ROOT / "include/pbl/soc/t5ai/tick.h"}"\n'
        run(
            "soc/t5ai/tick_profile.c",
            PROFILE_PREFIX + header + "T5AITickProfile t5ai_tick_profile;\n",
            PROFILE_MAIN,
            defines=("CONFIG_T5AI_TICK_PROFILE",),
        )


PROFILE_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define nullptr NULL
#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 0
static unsigned lock, registrations, logs;
static bool accept;
static uint32_t aon;
#define T5AI_AON_RTC_COUNT_LOW aon
static char line[512];
static void log_line(const char *fmt,...){
 assert(!lock);va_list a;va_start(a,fmt);vsnprintf(line,sizeof(line),fmt,a);va_end(a);++logs;
}
#define PBL_LOG_DBG(...) log_line(__VA_ARGS__)
static void pbl_irq_lock(void){assert(!lock);++lock;}
static void pbl_irq_unlock(void){assert(lock);--lock;}
typedef struct {void (*cb)(void *);} RegularTimerInfo;
static RegularTimerInfo *registered;
static void (*queued)(void *);
static bool system_task_add_callback_droppable(void (*cb)(void *),void *context){
 assert(!lock && !context);if(accept)queued=cb;return accept;
}
static void regular_timer_add_multisecond_callback(RegularTimerInfo *t,unsigned seconds){
 assert(!lock && seconds==60);registered=t;++registrations;
}
"""
PROFILE_MAIN = r"""
int main(void){
 aon=UINT32_MAX-31;t5ai_tick_profile_start();t5ai_tick_profile_start();assert(registrations==1);
 t5ai_tick_profile.irqs=3000;t5ai_tick_profile.ticks=60000;t5ai_tick_profile.maximum=69;
 t5ai_tick_profile.extended=2000;t5ai_tick_profile.early=1000;t5ai_tick_profile.aborted=2;
 accept=false;registered->cb(NULL);assert(!queued && !logs && t5ai_tick_profile.irqs==3000);
 accept=true;registered->cb(NULL);assert(queued && !logs);aon=32;queued(NULL);
 assert(strstr(line,"aon=64 irq=3000 ticks=60000 max=69 extended=2000 early=1000 aborted=2"));
 assert(!t5ai_tick_profile.irqs && !t5ai_tick_profile.ticks && !lock);
 aon+=1920000;registered->cb(NULL);queued(NULL);assert(logs==2 && strstr(line,"aon=1920000 irq=0"));
 return 0;
}
"""
