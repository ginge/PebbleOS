# SPDX-License-Identifier: Apache-2.0
"""Complete HCI units: latched producers, timed receive retries and idle waits."""

import unittest

from tools.tests.test_t5ai_hci_acl import HARNESS, run_hci_harness

UPPER = HARNESS[: HARNESS.index("int main(void)")]
UPPER = (
    UPPER.replace(
        "static uint8_t expected[8];",
        "static uint8_t expected[8];\nstatic unsigned tokens, waits, delivered_tokens;\n"
        "static pbl_tick_t wait_ticks;static bool race, queued;",
    )
    .replace(
        "void pbl_sem_give(struct pbl_sem *sem) { assert(false); }",
        "void pbl_sem_give(struct pbl_sem *sem) {assert(sem==&s_hci_work && !locks);tokens=1;}",
    )
    .replace(
        "int pbl_sem_take(struct pbl_sem *sem,pbl_timeout_t t) { assert(false);return -1; }",
        "int pbl_sem_take(struct pbl_sem *sem,pbl_timeout_t t) {\n"
        " assert(sem==&s_hci_work && !locks);++waits;wait_ticks=t.ticks;\n"
        " if(race){race=false;t5ai_hci_notify_work();}\n"
        " if(tokens){--tokens;++delivered_tokens;return 0;}return -1;}",
    )
    .replace(
        "int pbl_msgq_put(struct pbl_msgq *q,const void *msg,pbl_timeout_t t) { assert(false);return -1; }",
        "int pbl_msgq_put(struct pbl_msgq *q,const void *msg,pbl_timeout_t t) {\n"
        " assert(q==s_acl_tx_queue && msg && locks && !t.ticks);queued=true;return 0;}",
    )
    .replace(
        "bool t5ai_hci_ipc_sleep_ready(void) { assert(false);return false; }",
        "bool t5ai_hci_ipc_sleep_ready(void) {return true;}",
    )
)

UPPER += r"""
#ifdef CONFIG_T5AI_HCI_PROFILE
void t5ai_hci_ipc_get_diagnostics(T5AIHCIIpcDiagnostics *d){assert(false);}
struct pbl_thread *pebble_task_get_thread(PebbleTask t){assert(false);return NULL;}
char pebble_task_get_char(PebbleTask t){assert(false);return 0;}
void pbl_thread_stack_info(const struct pbl_thread *t,struct pbl_thread_stack_info *i){assert(false);}
enum pbl_thread_state pbl_thread_state(const struct pbl_thread *t){assert(false);return 0;}
pbl_prio_t pbl_thread_prio_get(const struct pbl_thread *t){assert(false);return 0;}
#endif
#ifdef CONFIG_T5AI_FLASH_PROFILE
void t5ai_flash_ipc_get_diagnostics(T5AIFlashDiagnostics *d){assert(false);}
#endif
uint32_t pbl_msgq_num_used(const struct pbl_msgq *q){assert(false);return 0;}
"""

UPPER_MAIN = r"""
int main(void){
 setup((uint64_t)UINT32_MAX+1000u);
 t5ai_hci_notify_work();t5ai_hci_notify_work();
 prv_wait_for_work((pbl_tick_t)now);
 assert(delivered_tokens==1 && !tokens);
#ifdef CONFIG_T5AI_HCI_EVENT_WAIT
#if defined(CONFIG_T5AI_HCI_PROFILE) || defined(CONFIG_T5AI_FLASH_PROFILE)
 assert(wait_ticks==pbl_ms_to_ticks(T5AI_HCI_TRACE_INTERVAL_MS));
 now+=pbl_ms_to_ticks(7);prv_wait_for_work((pbl_tick_t)now-pbl_ms_to_ticks(7));
 assert(wait_ticks==pbl_ms_to_ticks(T5AI_HCI_TRACE_INTERVAL_MS)-pbl_ms_to_ticks(7));
 prv_wait_for_work((pbl_tick_t)now-pbl_ms_to_ticks(T5AI_HCI_TRACE_INTERVAL_MS));
 assert(!wait_ticks); // Report already due; never extend its deadline.
#else
 assert(wait_ticks==PBL_TICK_FOREVER);
#endif
#else
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
 assert(wait_ticks==pbl_ms_to_ticks(T5AI_HCI_QUIET_POLL_INTERVAL_MS));
#else
 assert(wait_ticks==pbl_ms_to_ticks(T5AI_HCI_POLL_INTERVAL_MS));
#endif
#endif
 // A producer between the last work check and semaphore take remains latched.
 race=true;prv_wait_for_work((pbl_tick_t)now);assert(delivered_tokens==2 && !tokens);
 s_acl_tx_queue=&s_acl_tx_messages;
 assert(!ble_transport_to_ll_acl_impl(&packets[0].mbuf));
 assert(queued && tokens && !frees[0] && !locks);
 prv_wait_for_work((pbl_tick_t)now);assert(delivered_tokens==3 && !tokens);
#ifdef CONFIG_T5AI_HCI_PROFILE
 assert(s_work_notifications==3);
#ifdef CONFIG_T5AI_HCI_EVENT_WAIT
 assert(s_work_timeouts==2);
#else
 assert(!s_work_timeouts);
#endif
#endif
 return 0;
}
"""

LOWER = r"""
#include <assert.h>
#include <setjmp.h>
#include "third_party/nimble/transport/t5ai_hci_ipc.c"
static uint64_t now, start, release_after;
static unsigned tokens, notifications, attempts, delivered, waits, locks, irq_depth;
static bool race, run_worker;
static jmp_buf finished;
uint64_t pbl_uptime_ticks(void){return now;}
void pbl_irq_lock(void){++irq_depth;}
void pbl_irq_unlock(void){assert(irq_depth);--irq_depth;}
int pbl_mutex_lock_lr(struct pbl_mutex *m,pbl_timeout_t t,uintptr_t lr){
 assert(m==&s_async_mutex && !locks);++locks;return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m){assert(m==&s_async_mutex && locks==1);--locks;}
void t5ai_hci_notify_work(void){++notifications;}
void pbl_sem_give(struct pbl_sem *sem){assert(sem==&s_worker_wake);tokens=1;}
int pbl_sem_take(struct pbl_sem *sem,pbl_timeout_t t){
 assert(sem==&s_worker_wake && !locks && !irq_depth);++waits;
 if(race){race=false;pbl_sem_give(sem);}
 if(tokens){--tokens;return 0;}
 if(t.ticks==PBL_TICK_FOREVER){assert(run_worker);longjmp(finished,1);}
 assert(t.ticks<=pbl_ms_to_ticks(T5AI_HCI_RX_RETRY_DELAY_MS));
 now+=t.ticks;return -1;
}
void pbl_thread_sleep(pbl_timeout_t t){assert(false);}
void pbl_log(uint8_t l,const char *f,int n,const char *fmt,...){ }
void pbl_log_filtered(uint8_t l,const char *f,int n,const char *fmt,...){ }
bool t5ai_cp_mailbox_shared_range_valid(uintptr_t p,size_t n){assert(false);return false;}
status_t t5ai_cp_mailbox_send(const void *p,size_t n){assert(false);return E_ERROR;}
status_t t5ai_cp_mailbox_acknowledge_frame(const T5AICPMailboxFrame *f,bool handled){
 assert(f && handled);return S_SUCCESS;
}
static bool receive(const uint8_t *p,size_t n){
 assert(locks && p==s_rx_payload && n==1 && p[0]==42);++attempts;
 if(now-start<release_after)return false;
 ++delivered;return true;
}
static void prepare(uint64_t tick,uint64_t release){
 now=start=tick;release_after=release;
 attempts=delivered=waits=notifications=tokens=0;
 s_faulted=false;s_started=s_accept_frames=true;s_rx_count=1;s_rx_used=1;
 s_rx_payload[0]=42;
 s_rx[0].received=(pbl_tick_t)tick;s_rx[0].length=1;s_rx[0].subtype=T5AI_HCI_EVENT_TO_HOST;
 s_callbacks.event=receive;
}
static void drain(void){run_worker=true;if(!setjmp(finished))prv_worker(NULL);run_worker=false;}
int main(void){
 const uint64_t starts[]={0,UINT32_MAX-2ull,(uint64_t)UINT32_MAX+1000ull};
 for(unsigned i=0;i<sizeof(starts)/sizeof(*starts);++i){
  prepare(starts[i],pbl_ms_to_ticks(10));
  // The existing 32-bit received tick must be compared modulo its width.
  pbl_mutex_lock(&s_async_mutex,PBL_FOREVER);prv_retry_receive();pbl_mutex_unlock(&s_async_mutex);
  assert(!s_faulted && s_rx_count==1 && attempts==1);
#ifdef CONFIG_T5AI_HCI_EVENT_WAIT
  assert(prv_receive_wait().ticks==pbl_ms_to_ticks(T5AI_HCI_RX_RETRY_DELAY_MS));
  drain();assert(delivered==1 && !s_rx_count && !notifications);
  assert(now-start==pbl_ms_to_ticks(10) && waits==3);
  prepare(starts[i],UINT64_MAX);
  drain();assert(s_faulted && !s_rx_count && !delivered && notifications==1);
  assert(now-start==pbl_ms_to_ticks(T5AI_HCI_RX_TIMEOUT_MS));
  prepare(starts[i],UINT64_MAX);now+=pbl_ms_to_ticks(T5AI_HCI_RX_TIMEOUT_MS)-1;
  assert(prv_receive_wait().ticks==1);drain();assert(s_faulted && notifications==1);
#endif
 }
 // Both frame routing and a late fault notify the upper task in every configuration.
 s_rx_count=s_rx_used=0;notifications=tokens=0;
 T5AIMIPCFrame ignored={0}; // Not a command; still arrives through the real route.
 T5AICPMailboxFrame frame={.address=(uintptr_t)&ignored,.length=sizeof ignored};
 prv_route_frame(T5AI_MIPC_RX_LOGICAL_CHANNEL,&frame,NULL);
 assert(tokens && notifications==1 && s_pending_frame_count==1);
#ifdef CONFIG_T5AI_HCI_EVENT_WAIT
 assert(prv_receive_wait().ticks==PBL_TICK_FOREVER);
#endif
 race=true;drain();assert(!s_pending_frame_count && !tokens);
 prv_mark_faulted();assert(notifications==2 && prv_faulted() && !prv_started());
 assert(!locks && !irq_depth);return 0;
}
"""


class HciWorkTests(unittest.TestCase):
    def test_modeled_idle_wakes_and_notification_latency(self):
        main = r"""
int main(void){
 setup(0);unsigned scheduled_wakes=0;
 const uint64_t end=pbl_ms_to_ticks(60000);
 while(now<end){
  prv_wait_for_work(0);
  if(wait_ticks==PBL_TICK_FOREVER)break;
  assert(wait_ticks);now+=wait_ticks;++scheduled_wakes;
 }
#ifdef CONFIG_T5AI_HCI_EVENT_WAIT
 assert(!scheduled_wakes);
#else
#if defined(CONFIG_T5AI_RETAINED_SLEEP) || defined(CONFIG_T5AI_SLEEP_GUARD_TEST)
 assert(scheduled_wakes==600);
#else
 assert(scheduled_wakes==12000);
#endif
#endif
 // A signal at the blocking boundary is consumed in the same modeled tick.
 uint64_t arrival=now;race=true;prv_wait_for_work(0);
 assert(delivered_tokens==1 && now==arrival);
 return 0;
}
"""
        for retained in (False, True):
            for event_wait in (False, True):
                with self.subTest(retained=retained, event_wait=event_wait):
                    run_hci_harness(
                        UPPER + main,
                        {
                            "T5AI_POWER_SAVINGS": "y" if retained else "n",
                            "T5AI_RETAINED_SLEEP": "y" if retained else "n",
                            "T5AI_HCI_EVENT_WAIT": "y" if event_wait else "n",
                        },
                    )

    def test_complete_upper_notifications_and_wait_policies(self):
        for retained in (False, True):
            for event_wait in (False, True):
                for profile in ("none", "hci", "flash", "both"):
                    with self.subTest(
                        retained=retained, event_wait=event_wait, profile=profile
                    ):
                        values = {
                            "T5AI_POWER_SAVINGS": "y" if retained else "n",
                            "T5AI_RETAINED_SLEEP": "y" if retained else "n",
                            "T5AI_HCI_EVENT_WAIT": "y" if event_wait else "n",
                            "T5AI_HCI_PROFILE": "y"
                            if profile in ("hci", "both")
                            else "n",
                            "T5AI_FLASH_PROFILE": "y"
                            if profile in ("flash", "both")
                            else "n",
                        }
                        run_hci_harness(UPPER + UPPER_MAIN, values)

    def test_complete_ipc_profile_wake_counts(self):
        harness = LOWER.replace(
            "assert(!locks && !irq_depth);return 0;",
            "assert(!locks && !irq_depth);"
            "assert(s_diagnostics.work_notifications==1);"
            "\n#ifdef CONFIG_T5AI_HCI_EVENT_WAIT\n"
            "assert(s_diagnostics.work_timeouts==159);"
            "\n#else\nassert(!s_diagnostics.work_timeouts);\n#endif\nreturn 0;",
        )
        for event_wait in (False, True):
            with self.subTest(event_wait=event_wait):
                run_hci_harness(
                    harness,
                    {
                        "T5AI_HCI_PROFILE": "y",
                        "T5AI_HCI_EVENT_WAIT": "y" if event_wait else "n",
                    },
                    sources=("drivers/t5ai/mipc.c",),
                )

    def test_complete_ipc_worker_retries_deadlines_wrap_and_latched_frames(self):
        for retained in (False, True):
            for event_wait in (False, True):
                with self.subTest(retained=retained, event_wait=event_wait):
                    run_hci_harness(
                        LOWER,
                        {
                            "T5AI_POWER_SAVINGS": "y" if retained else "n",
                            "T5AI_RETAINED_SLEEP": "y" if retained else "n",
                            "T5AI_HCI_EVENT_WAIT": "y" if event_wait else "n",
                        },
                        sources=("drivers/t5ai/mipc.c",),
                    )


if __name__ == "__main__":
    unittest.main()
