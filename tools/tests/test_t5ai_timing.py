# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise mailbox synchronization storage and send critical-section scope."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run_c(source, headers):
    with tempfile.TemporaryDirectory() as temp:
        d = Path(temp)
        for name, contents in headers.items():
            p = d / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(contents)
        (d / "test.c").write_text(source)
        subprocess.run(
            [
                "cc",
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(d),
                "-I",
                str(ROOT / "include"),
                str(d / "test.c"),
                "-o",
                str(d / "test"),
            ],
            check=True,
        )
        subprocess.run([str(d / "test")], check=True)


def function(source, name):
    start = source.index(name + "(")
    start = source.rfind("\n", 0, start) + 1
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class T5TimingTest(unittest.TestCase):
    def test_mailbox_sync_storage_and_send(self):
        source = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        tx = source[
            source.index(
                "typedef struct {\n  T5AIMailboxMessage message"
            ) : source.index("} TxChannel;") + len("} TxChannel;")
        ]
        harness = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define T5AI_SHARED_ALIGNMENT 32
#define T5AI_MAILBOX_CHANNEL_COUNT 16
#define MAILBOX_TASK_STACK_SIZE 2048
#define MAILBOX_TASK_PRIORITY 4
#define MAILBOX_ACTIVE_TIMEOUT_MS 5000
#define E_OUT_OF_MEMORY -1
#define E_INVALID_ARGUMENT -2
#define E_INVALID_OPERATION -6
static bool t5ai_mailbox_channel_is_owner(void){return false;}
#define E_BUSY -3
#define E_ERROR -4
#define S_SUCCESS 0
#define FAILED(s) ((s)<0)
#define PBL_FOREVER UINT32_MAX
#define PBL_TICK_FOREVER UINT32_MAX
#define PBL_TICKS(x) (x)
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED 256u
struct pbl_sem { unsigned ready, limit; };
struct pbl_mutex { unsigned locked; };
struct pbl_thread { int unused; };
typedef int status_t;
typedef struct { uint32_t header, parameter1, parameter2, parameter3; } T5AIMailboxMessage;
typedef void (*T5AIMailboxTransmitHandler)(const T5AIMailboxMessage *, status_t, void *);
typedef enum { TxState_Idle, TxState_Pending, TxState_Active, TxState_Complete } TxState;
"""
            + tx
            + r"""
static TxChannel s_tx[16];
static bool s_initialized, s_started, s_faulted;
static struct pbl_sem s_wake = {.limit=1};
static struct pbl_thread s_task;
static _Alignas(32) unsigned char s_task_stack[MAILBOX_TASK_STACK_SIZE];
struct pbl_thread_attr {
 const char *name; void (*entry)(void *); void *arg; unsigned prio;
 bool privileged; void *stack; size_t stack_size;
};
static unsigned depth, creations;
static bool fail_create;
void pbl_irq_lock(void) { ++depth; }
void pbl_irq_unlock(void) { assert(depth); --depth; }
void pbl_sem_init(struct pbl_sem *s, unsigned initial, unsigned limit) { assert(!depth); s->ready=initial; s->limit=limit; }
void pbl_mutex_init(struct pbl_mutex *m) { assert(!depth); m->locked=0; }
static void prv_mailbox_task(void *arg) { (void)arg; }
int pbl_thread_create(struct pbl_thread *thread, const struct pbl_thread_attr *attr) {
 assert(!depth && thread==&s_task && attr->entry==prv_mailbox_task && attr->name);
 assert(attr->stack==s_task_stack && attr->stack_size==2048 && attr->privileged);
 assert(!attr->arg && attr->prio==4 && ((uintptr_t)attr->stack%32)==0);
 ++creations; return fail_create ? -1 : 0;
}
void pbl_mutex_lock(struct pbl_mutex *m, unsigned timeout) { assert(!depth && !m->locked && timeout==PBL_FOREVER); m->locked=1; }
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(!depth && m->locked); m->locked=0; }
int pbl_sem_take(struct pbl_sem *s, unsigned timeout) {
 assert(!depth);
 if (s->ready) { --s->ready; return 0; }
 assert(timeout==0); return -1;
}
int pbl_sem_give(struct pbl_sem *s) {
 assert(s==&s_wake && !depth);
 for (unsigned i=0;i<16;++i) {
   if (s_tx[i].state!=TxState_Pending) continue;
   s_tx[i].ack=(T5AIMailboxMessage){.parameter1=123+i};
   s_tx[i].completion.ready=1;
   s_tx[i].state=TxState_Complete;
 }
 return 0;
}
static bool prv_tx_channel(uint8_t channel, uint8_t *index) { if(channel>=16) return false; *index=channel; return true; }
static unsigned prv_milliseconds_to_ticks(unsigned n) { return n; }
static void prv_reset_after_timeout(unsigned i) { (void)i; assert(0); }
"""
        )
        harness += function(source, "t5ai_mailbox_channel_init")
        harness += function(source, "t5ai_mailbox_channel_send")
        harness += r"""
int main(void) {
 fail_create=true;
 assert(t5ai_mailbox_channel_init()==E_OUT_OF_MEMORY && !s_initialized && creations==1);
 fail_create=false;
 assert(t5ai_mailbox_channel_init()==0 && s_initialized && creations==2);
 assert(t5ai_mailbox_channel_init()==0 && creations==2);
 s_started=true;
 for (unsigned i=0;i<16;++i) {
   assert(s_tx[i].completion.limit==1);
   T5AIMailboxMessage message={.header=1}, ack;
   s_tx[i].completion.ready=1; // A stale completion is drained before publishing.
   assert(t5ai_mailbox_channel_send(i,&message,&ack,100)==0);
   assert(ack.parameter1==123+i && s_tx[i].state==TxState_Idle && !s_tx[i].mutex.locked);
 }
 assert(creations==2 && !depth);
 T5AIMailboxMessage message={0};
 assert(t5ai_mailbox_channel_send(16,&message,NULL,100)==E_INVALID_ARGUMENT);
 return 0;
}
"""
        run_c(harness, {})
