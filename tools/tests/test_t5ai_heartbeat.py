# SPDX-License-Identifier: Apache-2.0
"""Exercise heartbeat deadlines in the actual mailbox worker loop."""
import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body
from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


class T5AIHeartbeatTest(unittest.TestCase):
    def test_worker_deadlines_ownership_and_busy_paths(self):
        cp = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        constants = cp[cp.index("#define CP_LOG_TAG_SHIFT"):cp.index("typedef struct {")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
typedef uint32_t pbl_tick_t;
typedef int status_t;
#define S_SUCCESS 0
#define E_BUSY -1
#define E_INVALID_ARGUMENT -2
#define FAILED(s) ((s)<0)
#define PASSED(s) ((s)>=0)
#define PBL_TICK_FOREVER UINT32_MAX
#define PBL_TICKS(t) (t)
#define PBL_MSEC(t) (t)
#define pbl_ms_to_ticks(t) (t)
#define T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT 16
#define T5AI_MAILBOX_HEADER_CHANNEL_SHIFT 24
#define T5AI_MAILBOX_CHANNEL_INDEX_MASK 15
#define T5AI_MAILBOX_CHANNEL_COUNT 16
#define T5AI_MAILBOX_FRAME_SIZE 16
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED 256
#define MAILBOX_FAULT_POLL_MS 10
#define MAILBOX_BUSY_RETRY_MS 1
#define MBOX_INTERRUPT_ENABLE 1
#define MBOX_FIFO_NOT_EMPTY 2
#define PBL_LOG_ERR(...) ((void)0)
#define t5ai_reboot_trace_event(...) ((void)0)
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef void (*T5AIMailboxTransmitHandler)(const T5AIMailboxMessage *,status_t,void *);
typedef struct {uintptr_t address;size_t length;uint8_t snapshot[16];} T5AICPMailboxFrame;
typedef void (*T5AICPMailboxReceiveCallback)(uint8_t,const T5AICPMailboxFrame *,void *);
typedef struct {T5AICPMailboxReceiveCallback callback;void *context;} Route;
static Route s_routes[16];
static struct {unsigned unused;} s_wake;
static bool s_runtime_initialized,s_heartbeat_in_flight,s_faulted,s_started,s_reset_sent;
static uint64_t now,s_next_heartbeat_ticks;
static unsigned MBOX_CH1_CTRL,s_heartbeat_parameter;
static uint8_t s_control_sequence;
static T5AIMailboxMessage s_control_message;
typedef enum {TxState_Idle,TxState_Pending,TxState_Active} TxState;
typedef struct {
 TxState state;T5AIMailboxMessage message;T5AIMailboxTransmitHandler handler;
 void *context;bool synchronous;
} TxChannel;
static TxChannel s_tx[16];
static unsigned mode,sends,attempts,steps,drains,sleeps,token,depth;
static uint64_t sent_at,last_attempt;
static bool physical_done;
static jmp_buf finished;
static uint64_t pbl_uptime_ticks(void){return now;}
static unsigned prv_milliseconds_to_ticks(unsigned n){return n;}
static void pbl_irq_lock(void){assert(!depth);++depth;}
static void pbl_irq_unlock(void){assert(depth==1);--depth;}
static int pbl_sem_give(void *p){assert(p==&s_wake && !depth);token=1;return 0;}
static bool prv_tx_channel(unsigned ch,uint8_t *index){
 if(ch!=0x10){return false;}
 *index=0;return true;
}
static bool t5ai_mailbox_channel_faulted(void){return s_faulted;}
static void dcache_flush(void *p,size_t n){assert(p==&s_heartbeat_parameter && n==4);}
static bool prv_enqueue(unsigned c,const void *m){(void)c;(void)m;assert(0);return false;}
""" + constants
        harness += body(channel, "t5ai_mailbox_channel_wake")
        # The actual async submission owns a copy and rejects a busy channel.
        submit = body(channel, "t5ai_mailbox_channel_send_async")
        submit = submit.replace(
            "  uint8_t index;",
            "  ++attempts;last_attempt=now;uint8_t index;",
        ).replace(
            "  tx->message = *message;",
            "  assert(!sends || now>=sent_at+1000);++sends;sent_at=now;"
            "physical_done=false;tx->message = *message;",
        )
        harness += submit
        harness += body(cp, "prv_heartbeat_complete")
        harness += body(cp, "prv_heartbeat")
        harness += body(cp, "t5ai_cp_mailbox_heartbeat_poll")
        harness += body(cp, "prv_receive")
        harness += r"""
static bool prv_any_active(void){return s_tx[0].state!=TxState_Idle;}
static int pbl_sem_take(void *p,unsigned timeout){
 assert(p==&s_wake && !depth && ++steps<20000);
 if(now>=3500){longjmp(finished,1);}
 if(!s_runtime_initialized){
   assert(timeout==PBL_TICK_FOREVER && !attempts);
   s_runtime_initialized=true;s_started=true;
   t5ai_mailbox_channel_wake();
 }
 assert(timeout<=1000 || token);
 if(token){token=0;return 0;}
 assert(timeout>0 && timeout<=1000);
 if(prv_any_active())assert(timeout<=10);
 if(now+timeout>=3500){now=3500;longjmp(finished,1);}
 now+=timeout;return -1;
}
static bool prv_drain_rx(void){
 ++drains;
 if(mode==6 && sends){s_faulted=true;}
 if(s_tx[0].state!=TxState_Idle && now>=sent_at+10){
   // PENDING completes physically first; logical response retains parameter ownership.
   T5AIMailboxMessage ack={.parameter3=mode==5?1:2};
   s_tx[0].state=TxState_Idle;physical_done=true;
   s_tx[0].handler(&ack,S_SUCCESS,s_tx[0].context);
 }
 if(mode==5 && physical_done && s_heartbeat_in_flight && now>=sent_at+1500){
   T5AIMailboxMessage response={.header=2|0x80};
   prv_receive(&response,(void *)(uintptr_t)0x40);
   assert(!s_heartbeat_in_flight && response.parameter3==2);
 }
 return mode==1;
}
static int prv_expired_channel(void){return -1;}
static void prv_reset_after_timeout(unsigned c){(void)c;assert(0);}
static bool prv_publish_pending_acks(void){return mode!=2;}
static void prv_send_reset(void){if(mode!=3)s_reset_sent=true;}
static status_t prv_schedule_tx(void){return mode==4?E_BUSY:S_SUCCESS;}
static void pbl_thread_sleep(unsigned ms){assert(ms==1);++sleeps;now+=ms;}
"""
        harness += body(channel, "prv_mailbox_task")
        harness += r"""
int main(void){
 for(mode=0;mode<7;++mode){
   now=s_next_heartbeat_ticks=sent_at=last_attempt=0;
   sends=attempts=steps=drains=sleeps=token=depth=0;
   s_runtime_initialized=s_started=s_heartbeat_in_flight=s_faulted=false;
   s_reset_sent=mode!=3;physical_done=false;
   memset(s_tx,0,sizeof(s_tx));
   if(!setjmp(finished))prv_mailbox_task(NULL);
   assert(drains && !depth);
   if(mode==5){assert(sends==2 && attempts==2 && last_attempt==2000);}
   else if(mode==6){assert(sends==1);}
   else {assert(sends==4 && attempts==4 && last_attempt>=3000 && last_attempt<=3001);}
   if(mode>=1 && mode<=4)assert(sleeps>=3500);
 }
 // A busy logical slot must not overwrite transport-owned storage or retry in a spin.
 now=s_next_heartbeat_ticks=0;s_runtime_initialized=s_started=true;s_faulted=false;
 s_heartbeat_in_flight=false;s_tx[0].state=TxState_Active;
 s_tx[0].message=(T5AIMailboxMessage){.header=1234};
 sends=attempts=0;
 assert(t5ai_cp_mailbox_heartbeat_poll()==1000 && attempts==1 && !sends);
 assert(!s_heartbeat_in_flight && s_tx[0].message.header==1234);
 assert(t5ai_cp_mailbox_heartbeat_poll()==1000 && attempts==1);
 now=1000;s_tx[0].state=TxState_Idle;
 assert(t5ai_cp_mailbox_heartbeat_poll()==1000 && sends==1 && s_heartbeat_in_flight);
 T5AIMailboxMessage owned=s_tx[0].message;
 s_control_message.header=999;
 assert(s_tx[0].message.header==owned.header);
 // Failed physical completion releases only the in-flight flag.
 s_tx[0].handler(NULL,-1,NULL);assert(!s_heartbeat_in_flight);
 return 0;
}
"""
        run_c(harness, {})


if __name__ == "__main__":
    unittest.main()
