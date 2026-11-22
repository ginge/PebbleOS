# SPDX-License-Identifier: Apache-2.0
"""Execute T5AI HCI logical lease checks and latched quiet-work waits."""

import unittest

from tools.tests.native_t5ai_harness import ROOT, run
from tools.tests.test_t5ai_native_objects import body

UPPER = "third_party/nimble/transport/hci_t5ai.c"
LOWER = "third_party/nimble/transport/t5ai_hci_ipc.c"
BASIC = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t pbl_tick_t;
"""


class HciSleepTest(unittest.TestCase):
    def test_all_logical_leases_and_faults_hold_sleep(self):
        prefix = (
            BASIC
            + r"""
#define T5AI_HCI_REQUEST_CAPACITY 4
#define T5AI_HCI_EVENT_RESPONSE_CAPACITY 4
#define T5AI_HCI_NO_EXPECTED_SUBTYPE 255u
static bool s_request_in_use[4],s_event_response_in_use[4];
static bool started=true,faulted,s_accept_frames=true,s_system_reboot_pending;
static unsigned s_pending_frame_count,s_rx_count,locks;
static unsigned s_expected_subtype=255;
static bool prv_started(void){return started;}
static bool prv_faulted(void){return faulted;}
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){assert(locks);--locks;}
"""
        )
        source = (ROOT / LOWER).read_text()
        code = body(source, "prv_all_outbound_retired") + body(
            source, "t5ai_hci_ipc_sleep_ready"
        )
        main = r"""
int main(void){
 assert(t5ai_hci_ipc_sleep_ready());
 for(unsigned i=0;i<4;++i){
  s_request_in_use[i]=true;assert(!t5ai_hci_ipc_sleep_ready());s_request_in_use[i]=false;
  s_event_response_in_use[i]=true;assert(!t5ai_hci_ipc_sleep_ready());s_event_response_in_use[i]=false;
 }
 s_pending_frame_count=1;assert(!t5ai_hci_ipc_sleep_ready());s_pending_frame_count=0;
 s_rx_count=1;assert(!t5ai_hci_ipc_sleep_ready());s_rx_count=0;
 s_expected_subtype=3;assert(!t5ai_hci_ipc_sleep_ready());s_expected_subtype=255;
 s_system_reboot_pending=true;assert(!t5ai_hci_ipc_sleep_ready());s_system_reboot_pending=false;
 s_accept_frames=false;assert(!t5ai_hci_ipc_sleep_ready());s_accept_frames=true;
 started=false;assert(!t5ai_hci_ipc_sleep_ready());started=true;
 faulted=true;assert(!t5ai_hci_ipc_sleep_ready());faulted=false;
 assert(t5ai_hci_ipc_sleep_ready() && !locks);return 0;
}
"""
        run(LOWER, prefix, main, transform=lambda _: code)

    def test_quiet_wait_is_latched_and_busy_work_retries_promptly(self):
        prefix = (
            BASIC
            + r"""
#define PBL_MSEC(n) (n)
#define T5AI_HCI_QUIET_POLL_INTERVAL_MS 100u
#define T5AI_HCI_POLL_INTERVAL_MS 5u
static bool started=true,s_reset_scheduled,s_acl_tx_active,ipc_ready=true;
static void *s_acl_tx_queue=(void *)1;
static unsigned queued,wait_ms,tokens,takes;
static bool race;
static int s_hci_work;
static bool prv_started(void){return started;}
static bool t5ai_hci_ipc_sleep_ready(void){return ipc_ready;}
static unsigned pbl_msgq_num_used(void *q){assert(q);return queued;}
static int pbl_sem_give(int *sem){assert(sem==&s_hci_work);tokens=1;return 0;}
void t5ai_hci_notify_work(void);
static int pbl_sem_take(int *sem,unsigned timeout){
 assert(sem==&s_hci_work);wait_ms=timeout;
 if(race){race=false;ipc_ready=false;t5ai_hci_notify_work();}
 if(tokens){--tokens;++takes;return 0;}return -1;
}
"""
        )
        source = (ROOT / UPPER).read_text()
        code = "".join(
            body(source, n)
            for n in [
                "t5ai_hci_notify_work",
                "t5ai_hci_sleep_ready",
                "prv_wait_for_work",
            ]
        )
        main = r"""
int main(void){
 assert(t5ai_hci_sleep_ready());prv_wait_for_work(0);assert(wait_ms==100 && !takes);
 queued=1;assert(!t5ai_hci_sleep_ready());queued=0;
 s_acl_tx_active=true;assert(!t5ai_hci_sleep_ready());s_acl_tx_active=false;
 s_reset_scheduled=true;assert(!t5ai_hci_sleep_ready());s_reset_scheduled=false;
 ipc_ready=false;assert(!t5ai_hci_sleep_ready());prv_wait_for_work(0);assert(wait_ms==5);
 ipc_ready=true;t5ai_hci_notify_work();t5ai_hci_notify_work();
 prv_wait_for_work(0);assert(wait_ms==100 && takes==1 && !tokens);
 // Receive or enqueue between readiness check and blocking cannot lose its wake.
 race=true;prv_wait_for_work(0);assert(takes==2 && !tokens && !ipc_ready);
 prv_wait_for_work(0);assert(wait_ms==5 && takes==2);
 return 0;
}
"""
        run(
            UPPER,
            prefix,
            main,
            defines=("CONFIG_T5AI_RETAINED_SLEEP",),
            transform=lambda _: code,
        )

    def test_fallback_polling_interval_is_preserved(self):
        prefix = (
            BASIC
            + r"""
#define PBL_MSEC(n) (n)
#define T5AI_HCI_POLL_INTERVAL_MS 5u
static unsigned wait_ms;
static int s_hci_work;
static int pbl_sem_take(int *sem,unsigned ms){assert(sem==&s_hci_work);wait_ms=ms;return -1;}
"""
        )
        run(
            UPPER,
            prefix,
            "int main(void){prv_wait_for_work(0);assert(wait_ms==5);return 0;}",
            transform=lambda text: body(text, "prv_wait_for_work"),
        )


if __name__ == "__main__":
    unittest.main()
