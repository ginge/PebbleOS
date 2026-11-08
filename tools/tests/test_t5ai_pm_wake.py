# SPDX-License-Identifier: Apache-2.0
"""Exercise production PM wake routing independently of request replies."""

import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body
from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


class PMWakeTest(unittest.TestCase):
    def test_reply_event_interleaving_overflow_and_foreign_snapshot(self):
        source = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        constants = source[
            source.index("#define CP_LOG_TAG_SHIFT") : source.index("typedef struct {")
        ]
        declarations = source[
            source.index("typedef struct {") : source.index("static Route s_routes")
        ]
        events = source[
            source.index("#ifdef CONFIG_T5AI_PM_WAKE_EVENTS\n#define") : source.index(
                "\nstatic bool s_log_faulted;"
            )
        ]
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_T5AI_PM_WAKE_EVENTS 1
#define CONFIG_T5AI_NATIVE_ALARM 1
#define T5AI_PM_NATIVE_ALARM_WAKE 0x71
#define T5AI_PM_NATIVE_SLEEP_RESUMED 0x72
static unsigned native_wakes,native_resumes;
static void t5ai_native_alarm_notify(uint32_t gen,uint32_t tick,uint32_t abi){assert(gen==5 && tick==6 && abi==7);++native_wakes;}
static void t5ai_native_alarm_resume_notify(uint32_t gen,uint32_t tick,uint32_t abi){assert(gen==5 && tick==6 && abi==7);++native_resumes;}
#define T5AI_MAILBOX_FRAME_SIZE 16
#define T5AI_MAILBOX_CHANNEL_COUNT 16
#define T5AI_MAILBOX_CHANNEL_INDEX_MASK 15
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED 256
#define S_SUCCESS 0
#define E_AGAIN -1
#define E_INVALID_ARGUMENT -2
#define t5ai_reboot_trace_event(...) ((void)0)
typedef int status_t;
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef struct {uintptr_t address;size_t length;uint8_t snapshot[16];} T5AICPMailboxFrame;
typedef void (*T5AICPMailboxReceiveCallback)(uint8_t,const T5AICPMailboxFrame *,void *);
typedef struct {uint32_t mode,source,detail;} T5AIPMWakeEvent;
struct pbl_sem {unsigned count;};
static unsigned depth,callbacks;
static void pbl_irq_lock(void){assert(!depth);++depth;}
static void pbl_irq_unlock(void){assert(depth==1);--depth;}
static int pbl_sem_give(struct pbl_sem *s){++s->count;return 0;}
"""
            + constants
            + declarations
            + r"""
static Route s_routes[16];
static ReceiveQueue s_pm_queue;
static bool s_heartbeat_in_flight;
static void normal_route(uint8_t ch,const T5AICPMailboxFrame *f,void *ctx){
 assert(ch==0x42 && !ctx && f->address==(uintptr_t)f->snapshot);
 ++callbacks;
}
"""
            + events
        )
        for name in ["prv_enqueue", "prv_dequeue", "prv_receive"]:
            code += body(source, name)
        code += r"""
static void wake(unsigned n){
 T5AIMailboxMessage m={.header=0x10,.parameter1=1,.parameter2=2,.parameter3=n};
 prv_receive(&m,(void *)0x42);assert(m.header==0x10);
 memset(&m,0,sizeof(m)); // No event may retain foreign storage.
}
int main(void){
 T5AIPMWakeEvent event;
 T5AIMailboxMessage native={.header=0x71,.parameter1=5,.parameter2=6,.parameter3=7};
 prv_receive(&native,(void *)0x42);native.header=0x72;prv_receive(&native,(void *)0x42);
 assert(native_wakes==1 && native_resumes==1 && !s_pm_queue.count && !callbacks);
 assert(t5ai_cp_mailbox_take_wake_event(NULL)==E_INVALID_ARGUMENT);
 assert(t5ai_cp_mailbox_take_wake_event(&event)==E_AGAIN);
 T5AIMailboxMessage reply={.header=2};prv_receive(&reply,(void *)0x42);
 assert(s_pm_queue.count==1 && s_pm_queue.ready.count==1);
 wake(10);wake(11);assert(s_pm_queue.count==1 && s_wake_count==2);
 T5AICPMailboxFrame frame;assert(prv_dequeue(0x42,&frame));--s_pm_queue.ready.count;
 assert(((T5AIMailboxMessage *)frame.address)->header==2);
 assert(t5ai_cp_mailbox_take_wake_event(&event)==0 && event.detail==10 && event.mode==1 && event.source==2);
 assert(t5ai_cp_mailbox_take_wake_event(&event)==0 && event.detail==11);
 assert(!s_pm_queue.count && !s_pm_queue.ready.count);
 for(unsigned i=0;i<4;++i)wake(i);
 T5AIMailboxMessage overflow={.header=0x10};prv_receive(&overflow,(void *)0x42);
 assert(overflow.header==(0x10|256) && t5ai_cp_mailbox_wake_events_lost()==1);
 for(unsigned i=0;i<4;++i){assert(t5ai_cp_mailbox_take_wake_event(&event)==0 && event.detail==i);}
 assert(t5ai_cp_mailbox_take_wake_event(&event)==E_AGAIN);
 for(unsigned i=0;i<32;++i){wake(i);assert(t5ai_cp_mailbox_take_wake_event(&event)==0 && event.detail==i);}
 assert(t5ai_cp_mailbox_wake_events_lost()==1); // Loss cannot disappear by draining.
 s_wake_lost=UINT32_MAX;for(unsigned i=0;i<4;++i)wake(i);
 overflow.header=0x10;prv_receive(&overflow,(void *)0x42);assert(s_wake_lost==UINT32_MAX);
 s_routes[2].callback=normal_route;
 reply.header=2;prv_receive(&reply,(void *)0x42);assert(callbacks==1);
 overflow.header=0x10;prv_receive(&overflow,(void *)0x42);assert(callbacks==1);
 overflow.header=0x10;prv_receive(&overflow,(void *)0x40);
 assert(overflow.header==(0x10|256) && !depth);
 return 0;
}
"""
        run_c(code, {})
