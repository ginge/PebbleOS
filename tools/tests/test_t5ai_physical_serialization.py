# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Verify synchronous and asynchronous clients share a physical slot without reuse."""
import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body
from tools.tests.test_t5ai_timing import run_c

ROOT=Path(__file__).resolve().parents[2]

class PhysicalSerializationTest(unittest.TestCase):
    def test_flash_waits_for_async_slot_and_prevents_overtaking(self):
        source=(ROOT/'drivers/t5ai/mailbox_channel.c').read_text()
        harness=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int status_t;
#define S_SUCCESS 0
#define E_BUSY -11
#define E_INVALID_ARGUMENT -4
#define E_ERROR -1
#define PBL_FOREVER UINT32_MAX
#define PBL_TICK_FOREVER UINT32_MAX
#define PBL_TICKS(x) (x)
#define MAILBOX_ACTIVE_TIMEOUT_MS 5000u
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED 256u
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef void (*T5AIMailboxTransmitHandler)(const T5AIMailboxMessage *,status_t,void *);
enum {TxState_Idle,TxState_Pending,TxState_Active,TxState_Complete};
struct sem {unsigned count;};
typedef struct {
 unsigned state,mutex;struct sem completion;bool synchronous;
 T5AIMailboxMessage message,ack;T5AIMailboxTransmitHandler handler;void *context;
} TxChannel;
static TxChannel s_tx[1];
static struct sem s_wake;
static bool s_started=true,s_faulted;
static unsigned depth,retired,waits,actor=1;
static T5AIMailboxMessage first={.parameter3=123},second={.parameter3=456};
static unsigned prv_milliseconds_to_ticks(unsigned t){return t;}
static bool prv_tx_channel(unsigned ch,uint8_t *index){if(ch!=0x11)return false;*index=0;return true;}
static int pbl_mutex_lock(unsigned *m,unsigned t){
 if(*m && *m!=actor){assert(t==0);return -1;}
 assert(!*m);*m=actor;return 0;
}
static void pbl_mutex_unlock(unsigned *m){assert(*m==actor);*m=0;}
static void pbl_irq_lock(void){assert(!depth);++depth;}
static void pbl_irq_unlock(void){assert(depth);--depth;}
static int pbl_sem_give(struct sem *s){s->count=1;return 0;}
static int pbl_sem_take(struct sem *s,unsigned timeout);
static void prv_reset_after_timeout(unsigned i){(void)i;assert(false);}
"""
        harness+=body(source,'prv_finish_tx')
        harness+=body(source,'t5ai_mailbox_channel_send_async')
        harness+=body(source,'t5ai_mailbox_channel_send')
        harness+=r"""
static void retired_cb(const T5AIMailboxMessage *ack,status_t status,void *ctx){
 assert(actor==2 && status==0 && ctx==(void *)3 && ack->parameter3==123);++retired;
}
static int pbl_sem_take(struct sem *s,unsigned timeout){
 assert(s==&s_tx[0].completion && !depth);
 if(!s->count && timeout){
   assert(timeout==5000 && s_tx[0].state==TxState_Pending && ++waits<=2);
   actor=2;
   assert(t5ai_mailbox_channel_send_async(0x11,&first,retired_cb,(void *)3)==E_BUSY);
   assert(s_tx[0].message.parameter3==(waits==1?123u:456u));
   T5AIMailboxMessage ack=s_tx[0].message;
   prv_finish_tx(&s_tx[0],&ack,0);actor=1;
 }
 if(!s->count)return -1;
 --s->count;return 0;
}
int main(void){
 assert(t5ai_mailbox_channel_send_async(0x11,&first,retired_cb,(void *)3)==0);
 assert(!s_tx[0].mutex && s_tx[0].state==TxState_Pending);
 T5AIMailboxMessage ack;
 assert(t5ai_mailbox_channel_send(0x11,&second,&ack,5000)==0);
 assert(ack.parameter3==456 && waits==2 && retired==1);
 assert(s_tx[0].state==TxState_Idle && !s_tx[0].mutex && !depth && !s_faulted);
}
"""
        run_c(harness,{})
