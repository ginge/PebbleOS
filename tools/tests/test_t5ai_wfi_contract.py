# SPDX-License-Identifier: Apache-2.0
"""Exercise the production CP clock-vote contract."""

import unittest

from tools.tests.native_t5ai_harness import ROOT, run
from tools.tests.test_t5ai_native_objects import body

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


class CPClockContractTest(unittest.TestCase):
    def test_cp_contract_requires_success_and_invalidates_before_send(self):
        cp = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        code = (
            body(cp, "t5ai_cp_mailbox_wfi_clock_min_hz")
            + body(cp, "prv_pm_exchange")
            + body(cp, "prv_pm_request")
        )
        run(
            "drivers/t5ai/cp_mailbox.c",
            PM_PREFIX,
            PM_MAIN,
            defines=("CONFIG_T5AI_TICKLESS_WFI",),
            transform=lambda _: code,
        )
