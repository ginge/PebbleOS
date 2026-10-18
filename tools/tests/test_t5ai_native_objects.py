# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Check retry ownership and persistent synchronization in local T5AI drivers."""
import re
import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


def body(source, name):
    match = re.search(r"(?m)^[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source)
    start = match.start()
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


NATIVE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef int status_t;
typedef unsigned pbl_tick_t;
#define S_SUCCESS 0
#define E_OUT_OF_MEMORY -1
#define E_ERROR -2
#define E_INVALID_OPERATION -3
#define E_INVALID_ARGUMENT -4
#define FAILED(x) ((x)<0)
#define PASSED(x) ((x)>=0)
#define PBL_FOREVER 0xffffffffu
#define PBL_SEM_INITIALIZER(n, max) {.count=(n),.limit=(max)}
#define PBL_SEM_DEFINE(name,n,max) struct pbl_sem name=PBL_SEM_INITIALIZER(n,max)
#define PBL_MUTEX_DEFINE(name) struct pbl_mutex name
#define PBL_THREAD_STACK_DEFINE(name,size) static _Alignas(32) uint8_t name[size]
struct pbl_sem {unsigned count,limit;};
struct pbl_mutex {unsigned locked;};
struct pbl_thread {unsigned id;};
struct pbl_thread_attr {
 const char *name; void (*entry)(void *); void *arg; unsigned prio;
 bool privileged; void *stack; size_t stack_size;
};
static unsigned creates,sem_inits,mutex_inits;
static bool fail_create;
static struct pbl_thread *created_thread;
static void *created_stack;
static size_t expected_stack_size;
static unsigned expected_prio;
static void expected_entry(void *arg) {(void)arg;}
static int pbl_thread_create(struct pbl_thread *t,const struct pbl_thread_attr *a) {
 assert(a->entry==expected_entry && a->name && !a->arg && a->privileged);
 assert(a->stack_size==expected_stack_size && a->prio==expected_prio);
 assert((uintptr_t)a->stack%32==0 && t->id==0);
 if(created_thread)assert(created_thread==t && created_stack==a->stack);
 created_thread=t;created_stack=a->stack;++creates;
 if(fail_create)return -1;
 t->id=1;return 0;
}
static void pbl_mutex_lock(struct pbl_mutex *m,unsigned timeout) {
 assert(timeout==PBL_FOREVER && !m->locked);m->locked=1;
}
static void pbl_mutex_unlock(struct pbl_mutex *m) {assert(m->locked);m->locked=0;}
"""


class T5AINativeObjectsTest(unittest.TestCase):
    def test_cp_runtime_activation_preserves_pm_queue_without_worker(self):
        source = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        declarations = source[source.index("static ReceiveQueue s_pm_queue"):
                              source.index("static T5AIMailboxMessage s_control_message")]
        harness = NATIVE + r"""
#define CP_QUEUE_DEPTH 4
#define T5AI_MAILBOX_CHANNEL_COUNT 16
#define T5AI_MAILBOX_RX_CHANNEL_BASE 64
typedef struct {struct pbl_sem ready;} ReceiveQueue;
""" + declarations + r"""
static bool s_runtime_initialized,fail_init=true;
static unsigned attempts,registers,starts,wakes;
static void prv_receive(void *m,void *ctx){(void)m;(void)ctx;}
static int t5ai_mailbox_channel_init(void){++attempts;return fail_init?E_ERROR:0;}
static int t5ai_mailbox_channel_register(unsigned channel,void (*f)(void *,void *),void *ctx){
 assert(channel==64+registers && f==prv_receive && (uintptr_t)ctx==channel);
 ++registers;return 0;
}
static int t5ai_mailbox_channel_start(void){++starts;return 0;}
static void t5ai_mailbox_channel_wake(void){assert(s_runtime_initialized);++wakes;}
""" + body(source, "prv_ensure_runtime") + r"""
int main(void){
 (void)pbl_thread_create;(void)pbl_mutex_lock;(void)pbl_mutex_unlock;
 (void)s_control_mutex;(void)s_pm_mutex;(void)s_log_mutex;
 s_pm_queue.ready.count=1;
 assert(prv_ensure_runtime()==E_ERROR && !s_runtime_initialized);
 assert(s_pm_queue.ready.count==1 && !registers && !wakes);
 fail_init=false;
 assert(prv_ensure_runtime()==0 && s_runtime_initialized);
 assert(attempts==2 && registers==16 && starts==1 && wakes==1);
 assert(prv_ensure_runtime()==0);
 assert(attempts==2 && registers==16 && starts==1 && wakes==1);
 assert(s_pm_queue.ready.count==1 && !sem_inits && !mutex_inits && !creates);
 return 0;
}
"""
        run_c(harness, {})

    def test_mb_ipc_registration_retry_does_not_reset_socket_objects(self):
        source = (ROOT / "drivers/t5ai/mb_ipc.c").read_text()
        harness = NATIVE + r"""
#define MB_IPC_SOCKET_COUNT 2
#define MB_IPC_RX_CHANNEL 65
struct Socket {struct pbl_mutex mutex;struct pbl_sem tx_ready,rx_ready;};
static struct Socket s_sockets[2];
static bool s_initialized,s_sync_initialized,fail_register=true;
static unsigned registrations;
static void pbl_mutex_init(struct pbl_mutex *m){m->locked=0;++mutex_inits;}
static void pbl_sem_init(struct pbl_sem *s,unsigned initial,unsigned limit){
 s->count=initial;s->limit=limit;++sem_inits;
}
static void prv_receive_callback(void *m,void *ctx){(void)m;(void)ctx;}
static int t5ai_cp_mailbox_register(unsigned channel,void (*f)(void *,void *),void *ctx){
 assert(channel==65 && f==prv_receive_callback && !ctx);++registrations;
 return fail_register?E_ERROR:0;
}
""" + body(source, "t5ai_mb_ipc_init") + r"""
int main(void){
 (void)pbl_thread_create;(void)pbl_mutex_lock;(void)pbl_mutex_unlock;
 assert(t5ai_mb_ipc_init()==E_ERROR && !s_initialized && s_sync_initialized);
 assert(sem_inits==4 && mutex_inits==2);
 s_sockets[0].rx_ready.count=1;s_sockets[1].tx_ready.count=1;
 fail_register=false;
 assert(t5ai_mb_ipc_init()==0 && s_initialized);
 assert(t5ai_mb_ipc_init()==0 && registrations==2);
 assert(sem_inits==4 && mutex_inits==2);
 assert(s_sockets[0].rx_ready.count==1 && s_sockets[1].tx_ready.count==1);
 assert(s_sockets[0].rx_ready.limit==1 && s_sockets[1].tx_ready.limit==1);
 return 0;
}
"""
        run_c(harness, {})

    def test_hci_worker_survives_registration_failure_and_stop_restart(self):
        source = (ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c").read_text()
        declarations = source[source.index("static PBL_MUTEX_DEFINE(s_sync_mutex)"):
                              source.index("static volatile uint8_t s_expected_subtype")]
        harness = NATIVE + r"""
#define T5AI_HCI_WORKER_STACK_SIZE 3072
#define T5AI_HCI_WORKER_PRIORITY 3
#define T5AI_MIPC_RX_LOGICAL_CHANNEL 65
#define T5AI_HCI_RESET 2
#define T5AI_HCI_REGISTER_CALLBACKS 7
#define T5AI_HCI_RECOVERY_DRAIN_TIMEOUT_MS 50
#define T5AI_HCI_OWNERSHIP_POLL_INTERVAL_MS 1
#define pbl_ms_to_ticks(x) (x)
#define PBL_MSEC(x) (x)
#define prv_worker expected_entry
typedef struct {void (*event)(void);void (*acl)(void);} T5AIHCICallbacks;
static T5AIHCICallbacks s_callbacks;
static bool s_started,s_faulted,s_accept_frames,fail_register;
static unsigned s_pending_frame_head,s_pending_frame_count,registrations,exchanges;
static bool s_request_in_use[8],s_event_response_in_use[8];
""" + declarations + r"""
static bool prv_started(void){return s_started;}
static bool prv_faulted(void){return s_faulted;}
static bool prv_pending_empty(void){return true;}
static void prv_set_started(bool v){s_started=v;}
static void pbl_sem_give(struct pbl_sem *s){if(s->count<s->limit)++s->count;}
static unsigned pbl_uptime_ticks(void){return 0;}
static void pbl_thread_sleep(unsigned t){(void)t;assert(0);}
static void prv_route_frame(void *m,void *ctx){(void)m;(void)ctx;}
static int t5ai_cp_mailbox_register(unsigned c,void (*f)(void *,void *),void *ctx){
 assert(c==65 && f==prv_route_frame && !ctx);++registrations;return fail_register?E_ERROR:0;
}
static void t5ai_cp_mailbox_unregister(unsigned c){assert(c==65);}
static int prv_locked_exchange(unsigned type,const void *p,size_t n){
 assert((type==2 || type==7) && !p && !n);++exchanges;return 0;
}
static void callback(void){}
""" + body(source, "t5ai_hci_ipc_start") + r"""
int main(void){
 assert(s_sync_mutex.locked==0 && s_exchange_ready.limit==1 && !s_exchange_ready.count);
 expected_stack_size=3072;expected_prio=3;
 T5AIHCICallbacks cb={callback,callback};
 assert(t5ai_hci_ipc_start(NULL)==E_INVALID_ARGUMENT && !creates);
 fail_create=true;
 assert(t5ai_hci_ipc_start(&cb)==E_OUT_OF_MEMORY && creates==1 && !s_worker_created);
 assert(!s_callbacks.event && !s_callbacks.acl && !registrations);
 fail_create=false;fail_register=true;
 assert(t5ai_hci_ipc_start(&cb)==E_ERROR && creates==2 && s_worker_created);
 assert(!s_callbacks.event && !s_started);
 fail_register=false;
 assert(t5ai_hci_ipc_start(&cb)==0 && creates==2 && s_started && s_accept_frames);
 assert(t5ai_hci_ipc_start(&cb)==E_INVALID_ARGUMENT && creates==2);
 // The existing stop path leaves this persistent worker alive.
 s_started=false;
 assert(t5ai_hci_ipc_start(&cb)==0 && creates==2);
 // Recovering startup must retain descriptors still owned by the CP.
 s_started=false;s_faulted=true;s_request_in_use[0]=true;
 assert(t5ai_hci_ipc_start(&cb)==0 && creates==2 && s_request_in_use[0]);
 assert(!s_faulted && exchanges==6 && !sem_inits && !mutex_inits);
 return 0;
}
"""
        run_c(harness, {})
