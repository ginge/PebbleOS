# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Run production failure paths with pthread lock/condition scheduling."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tests.test_t5ai_native_objects import body

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    if os.environ.get("A2_BASELINE"):
        return subprocess.check_output(
            ["git", "show", f"f3ebbee4c:{path}"], cwd=ROOT, text=True
        )
    return (ROOT / path).read_text()


def run(code):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "probe.c"
        exe = Path(tmp) / "probe"
        c.write_text(code)
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-pthread", str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=8)


COMMON = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
typedef int status_t;
#define S_SUCCESS 0
#define E_ERROR -1
#define E_AGAIN -2
#define E_INVALID_OPERATION -3
#define E_OUT_OF_RESOURCES -4
#define FAILED(x) ((x)<0)
#define PBL_FOREVER UINT32_MAX
#define PBL_TICKS(x) (x)
#define pbl_ms_to_ticks(x) (x)
#define PBL_MUTEX_DEFINE(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static pthread_mutex_t irq = PTHREAD_MUTEX_INITIALIZER;
static void pbl_irq_lock(void) { assert(!pthread_mutex_lock(&irq)); }
static void pbl_irq_unlock(void) { assert(!pthread_mutex_unlock(&irq)); }
static __attribute__((unused)) struct timespec deadline(void) {
  struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_sec+=2;return t;
}
"""


class MailboxFailuresTest(unittest.TestCase):
    def test_hci_exhaustion_does_not_wait_for_logger(self):
        hci = source("third_party/nimble/transport/t5ai_hci_ipc.c")
        channel = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        adapter = (ROOT / "soc/t5ai/mailbox_logging.c").read_text()
        code = COMMON + r"""
#define T5AI_HCI_PENDING_FRAME_CAPACITY 8
#define T5AI_MIPC_RX_LOGICAL_CHANNEL 65
#define __ATOMIC_ACQUIRE 2
#define __ATOMIC_RELEASE 3
typedef struct {uint32_t header,object,result;} T5AIMIPCFrame;
typedef struct {uintptr_t address;size_t length;} T5AICPMailboxFrame;
typedef struct {T5AIMIPCFrame frame;} T5AIHCIPendingFrame;
static T5AIHCIPendingFrame s_pending_frames[8];
static uint8_t s_pending_frame_head,s_pending_frame_count=8;
static bool s_accept_frames=true,s_started=true,s_faulted;
static struct {uint32_t count,header,object;bool reported;} s_queue_fault;
static pthread_mutex_t state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pulse_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool logger_ready,mailbox_done,report_attempted,reported;
static unsigned acks,physical_acks,drains,supervision,wakes;
static int s_worker_wake,s_wake;
static bool s_reset_sent=true;
static unsigned MBOX_CH1_CTRL;
#define MBOX_INTERRUPT_ENABLE 1
#define MBOX_FIFO_NOT_EMPTY 2
#define MAILBOX_FAULT_POLL_MS 10
#define MAILBOX_BUSY_RETRY_MS 1
#define PBL_TICK_FOREVER UINT32_MAX
#define PBL_MSEC(x) (x)
typedef unsigned pbl_tick_t;
static int s_task;
static _Thread_local void *current;
static void *pbl_thread_current(void){return current;}
static bool mcu_state_is_isr(void){return false;}
static bool t5ai_mailbox_channel_is_owner(void);
typedef struct {int unused;} LogState;
static LogState state;
static LogState *t5ai_stock_get_log_state(void){
  assert(!pthread_mutex_lock(&state_lock));return &state;
}
static int t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *f,bool ok){
  (void)f;assert(ok);++acks;return 0;
}
static int pbl_sem_give(int *s){if(s==&s_worker_wake)++wakes;else assert(s==&s_wake);return 0;}
static void pressure_log(void){
  assert(!pthread_mutex_lock(&gate));report_attempted=true;
  pthread_cond_broadcast(&changed);pthread_mutex_unlock(&gate);
  assert(!pthread_mutex_lock(&state_lock));
  assert(!pthread_mutex_lock(&pulse_lock));
  reported=true;pthread_mutex_unlock(&pulse_lock);pthread_mutex_unlock(&state_lock);
}
#define PBL_LOG_ERR(...) pressure_log_values(__VA_ARGS__)
static void pressure_log_values(const char *fmt,...){(void)fmt;pressure_log();}
"""
        code += body(channel, "t5ai_mailbox_channel_is_owner")
        code += body(adapter, "kernel_applib_get_log_state")
        code += body(hci, "prv_set_started") + body(hci, "prv_mark_faulted")
        code += body(hci, "prv_queue_pending_frame")
        if "prv_report_queue_fault" in hci:
            code += body(hci, "prv_report_queue_fault")
        else:
            code += "static void prv_report_queue_fault(void){pressure_log();}\n"
        code += body(hci, "prv_route_frame")
        code += r"""
static void *logger(void *unused){
  (void)unused;
  pthread_mutex_lock(&state_lock);pthread_mutex_lock(&pulse_lock);
  pthread_mutex_lock(&gate);logger_ready=true;pthread_cond_broadcast(&changed);
  struct timespec t=deadline();
  while(!mailbox_done)assert(pthread_cond_timedwait(&changed,&gate,&t)==0);
  pthread_mutex_unlock(&gate);
  pthread_mutex_unlock(&pulse_lock);pthread_mutex_unlock(&state_lock);return NULL;
}
static void *reporter(void *unused){
  (void)unused;
  // Model the independent worker reaching the shared logger while it is owned.
  pressure_log();
  prv_report_queue_fault();return NULL;
}
static bool prv_any_active(void){return false;}
static unsigned prv_milliseconds_to_ticks(unsigned t){return t;}
static unsigned t5ai_mb_ipc_async_poll(void){return UINT32_MAX;}
static unsigned t5ai_cp_mailbox_heartbeat_poll(void){
  assert(kernel_applib_get_log_state()==NULL); // before either logging lock
  if(drains==32){
    assert(s_faulted && !s_started);
    pthread_mutex_lock(&gate);mailbox_done=true;pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);pthread_exit(NULL);
  }
  ++supervision;return 1000;
}
static int pbl_sem_take(int *s,unsigned t){(void)t;assert(s==&s_wake);return 0;}
static bool prv_drain_rx(void){
  T5AIMIPCFrame pending={.header=0x1234,.object=0x5678};
  T5AICPMailboxFrame frame={.address=(uintptr_t)&pending,.length=sizeof(pending)};
  prv_route_frame(65,&frame,NULL);++drains;return true;
}
static int prv_expired_channel(void){return -1;}
static void prv_reset_after_timeout(unsigned c){(void)c;assert(0);}
static bool prv_publish_pending_acks(void){assert(acks==drains);++physical_acks;return true;}
static void prv_send_reset(void){assert(0);}
static int prv_schedule_tx(void){return 0;}
#define E_BUSY -5
static void pbl_thread_sleep(unsigned t){assert(t==1);}
"""
        code += body(channel, "prv_mailbox_task")
        code += r"""
static void *owner(void *unused){current=&s_task;prv_mailbox_task(unused);return NULL;}
int main(void){
  pthread_t l,w,o;
  pthread_create(&l,NULL,logger,NULL);
  pthread_mutex_lock(&gate);
  while(!logger_ready)pthread_cond_wait(&changed,&gate);
  pthread_mutex_unlock(&gate);
  pthread_create(&w,NULL,reporter,NULL);
  pthread_mutex_lock(&gate);
  while(!report_attempted)pthread_cond_wait(&changed,&gate);
  pthread_mutex_unlock(&gate);
  pthread_create(&o,NULL,owner,NULL);
  pthread_join(o,NULL);pthread_join(l,NULL);pthread_join(w,NULL);
  assert(physical_acks==32 && acks==32 && drains==32 && supervision==32 && wakes==32 && reported);
  assert(s_queue_fault.count==32 && s_queue_fault.header==0x1234 &&
         s_queue_fault.object==0x5678 && s_queue_fault.reported);
  // Saturation preserves the first descriptor rather than wrapping or replacing it.
  s_queue_fault.count=UINT32_MAX;
  T5AIMIPCFrame p={.header=99,.object=88};
  T5AICPMailboxFrame f={.address=(uintptr_t)&p,.length=sizeof(p)};
  prv_route_frame(65,&f,NULL);
  assert(s_queue_fault.count==UINT32_MAX && s_queue_fault.header==0x1234);
  LogState *normal=kernel_applib_get_log_state();assert(normal==&state);
  pthread_mutex_unlock(&state_lock);
}
"""
        run(code)


    def test_cp_owner_apis_reject_before_locks_and_waits(self):
        cp = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        code = COMMON + r"""
#define E_INVALID_ARGUMENT -5
#define PASSED(x) ((x)>=0)
#define PBL_MSEC(x) (x)
#define CP_RUNTIME_TIMEOUT_MS 5000
#define CP_LOG_ASSERT_OUT_COMMAND 4
#define CP_LOG_STATIC_TAG 0xffff
#define CP_LOG_TAG_SHIFT 16
#define CP_LOG_CHANNEL 0x1e
#define CP_PM_RX_CHANNEL 0x42
struct pbl_sem {int unused;};
typedef struct {uint32_t header,parameter1,parameter2,parameter3;} T5AIMailboxMessage;
typedef struct {uintptr_t address;} T5AICPMailboxFrame;
static struct {struct pbl_sem ready;} s_pm_queue;
static PBL_MUTEX_DEFINE(s_control_mutex);
static PBL_MUTEX_DEFINE(s_log_mutex);
static bool s_log_faulted;
static uint8_t s_log_buffer[160];
static T5AIMailboxMessage s_log_message;
static bool t5ai_mailbox_channel_is_owner(void){return true;}
static bool mcu_state_is_isr(void){return false;}
static bool pbl_kernel_is_running(void){assert(0);return true;}
static status_t prv_ensure_runtime(void){assert(0);return 0;}
static bool t5ai_mailbox_channel_faulted(void){assert(0);return false;}
static int pbl_mutex_lock(pthread_mutex_t *m,unsigned t){
 (void)m;(void)t;assert(0);return 0;
}
static void pbl_mutex_unlock(pthread_mutex_t *m){(void)m;assert(0);}
static int pbl_sem_take(struct pbl_sem *s,unsigned t){(void)s;(void)t;assert(0);return -1;}
static bool prv_dequeue(unsigned ch,T5AICPMailboxFrame *f){(void)ch;(void)f;assert(0);return false;}
static status_t prv_wait_for_log_buffer_release(bool running){(void)running;assert(0);return 0;}
static void t5ai_cache_clean(const void *p,size_t n){(void)p;(void)n;assert(0);}
static status_t t5ai_mailbox_channel_send(unsigned ch,const T5AIMailboxMessage *m,
                                        T5AIMailboxMessage *a,unsigned t){
 (void)ch;(void)m;(void)a;(void)t;assert(0);return 0;
}
#define t5ai_mailbox_channel_bootstrap_send t5ai_mailbox_channel_send
"""
        code += body(cp, "t5ai_cp_mailbox_lock")
        code += body(cp, "t5ai_cp_mailbox_receive")
        code += body(cp, "t5ai_cp_mailbox_log")
        code += r"""
int main(void){
 pbl_irq_lock();pbl_irq_unlock();
 T5AICPMailboxFrame frame;
 assert(t5ai_cp_mailbox_lock()==E_INVALID_OPERATION);
 assert(t5ai_cp_mailbox_receive(CP_PM_RX_CHANNEL,&frame)==E_INVALID_OPERATION);
 assert(t5ai_cp_mailbox_log("pressure",8)==E_INVALID_OPERATION);
}
"""
        run(code)
