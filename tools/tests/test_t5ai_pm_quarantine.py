# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise PM quarantine with two callers contending on a real mutex."""
import unittest

from tools.tests.test_t5ai_mailbox_failures import COMMON, run, source
from tools.tests.test_t5ai_native_objects import body


class PMQuarantineTest(unittest.TestCase):
    def test_queued_pm_caller_preserves_timeout_quarantine(self):
        cp = source("drivers/t5ai/cp_mailbox.c")
        code = COMMON + r"""
#define CP_PM_RX_CHANNEL 0x42
#define CP_PM_TX_CHANNEL 0x12
#define CP_RUNTIME_TIMEOUT_MS 5000
#define CP_PM_TIMEOUT_MS 100
#define CP_QUEUE_DEPTH 4
struct pbl_sem {pthread_mutex_t mutex;pthread_cond_t cond;unsigned count;};
#define SEM_INIT {PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0}
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef struct {uintptr_t address;size_t length;uint8_t snapshot[16];} T5AICPMailboxFrame;
typedef struct {T5AICPMailboxFrame frames[4];uint8_t head,count;struct pbl_sem ready;} ReceiveQueue;
static ReceiveQueue s_pm_queue={.ready=SEM_INIT};
static PBL_MUTEX_DEFINE(s_pm_mutex);
static T5AIMailboxMessage s_pm_message;
static bool s_pm_faulted;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool first_sent,second_waiting,late_queued;
static unsigned sends;
static bool healthy=true;
static _Thread_local unsigned caller;
static status_t result[3];
static bool prv_enqueue(uint8_t,const T5AIMailboxMessage *);
static int pbl_sem_give(struct pbl_sem *s){
 pthread_mutex_lock(&s->mutex);++s->count;pthread_cond_broadcast(&s->cond);
 pthread_mutex_unlock(&s->mutex);return 0;
}
static int pbl_sem_take(struct pbl_sem *s,unsigned timeout){
 pthread_mutex_lock(&s->mutex);
 if(timeout && !s->count){
   pthread_mutex_lock(&gate);
   while(!second_waiting)pthread_cond_wait(&changed,&gate);
   pthread_mutex_unlock(&gate);
   struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=30000000;
   if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
   int rc=0;
   while(!s->count && rc!=ETIMEDOUT)rc=pthread_cond_timedwait(&s->cond,&s->mutex,&t);
 }
 int rc=s->count?0:-1;if(s->count)--s->count;
 pthread_mutex_unlock(&s->mutex);return rc;
}
static int pbl_mutex_lock(pthread_mutex_t *m,unsigned timeout){
 assert(timeout==PBL_FOREVER);
 if(caller==2){
   pthread_mutex_lock(&gate);second_waiting=true;pthread_cond_broadcast(&changed);
   pthread_mutex_unlock(&gate);
 }
 int rc=pthread_mutex_lock(m);
 if(caller==2){
   // Response arrives after caller 2 acquires the transaction mutex.
   T5AIMailboxMessage response={.header=2};
   assert(prv_enqueue(CP_PM_RX_CHANNEL,&response));late_queued=true;
 }
 return rc;
}
static void pbl_mutex_unlock(pthread_mutex_t *m){assert(!pthread_mutex_unlock(m));}
static status_t prv_ensure_runtime(void){return 0;}
static __attribute__((unused)) bool t5ai_mailbox_channel_is_owner(void){return false;}
static status_t t5ai_mailbox_channel_send(unsigned channel,const T5AIMailboxMessage *m,
                                        T5AIMailboxMessage *ack,unsigned timeout){
 (void)ack;assert(channel==CP_PM_TX_CHANNEL && timeout==5000 && m->header==2);
 ++sends;
 if(healthy){T5AIMailboxMessage response={.header=m->header};
   assert(prv_enqueue(CP_PM_RX_CHANNEL,&response));return 0;}
 pthread_mutex_lock(&gate);first_sent=true;pthread_cond_broadcast(&changed);
 pthread_mutex_unlock(&gate);return 0;
}
"""
        code += body(cp, "prv_enqueue") + body(cp, "prv_dequeue")
        code += body(cp, "prv_pm_request")
        code += r"""
static void *request(void *arg){caller=(uintptr_t)arg;result[caller]=prv_pm_request(2,caller,1);return NULL;}
int main(void){
 assert(prv_pm_request(2,7,1)==S_SUCCESS);
 assert(prv_pm_request(2,8,1)==S_SUCCESS);
 assert(sends==2 && !s_pm_faulted && s_pm_queue.count==0);
 healthy=false;sends=0;
 pthread_t a,b;pthread_create(&a,NULL,request,(void *)1);
 pthread_mutex_lock(&gate);
 while(!first_sent)pthread_cond_wait(&changed,&gate);
 pthread_mutex_unlock(&gate);
 pthread_create(&b,NULL,request,(void *)2);
 pthread_join(a,NULL);pthread_join(b,NULL);
 assert(result[1]==E_AGAIN && result[2]==E_INVALID_OPERATION);
 assert(sends==1 && s_pm_faulted && late_queued);
 assert(s_pm_queue.count==1 && s_pm_queue.ready.count==1); // no drain by caller 2
 assert(s_pm_message.parameter1==1); // quarantined request remains untouched
 result[0]=prv_pm_request(2,3,1);
 assert(result[0]==E_INVALID_OPERATION && sends==1 && s_pm_queue.count==1);
}
"""
        run(code)
