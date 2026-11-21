# SPDX-License-Identifier: Apache-2.0
"""Complete transport, NimBLE mbuf and kernel tick units with modeled IPC/time."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_config import load_config

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <string.h>
#include "third_party/nimble/transport/hci_t5ai.c"
struct packet {
  struct os_mbuf mbuf;
  struct os_mbuf_pkthdr header;
  uint8_t data[8];
};
static struct packet packets[2];
static struct os_mempool mempool;
static struct os_mbuf_pool pool={.omp_pool=&mempool};
static unsigned frees[2], sends, polls, resets, warnings, sleeps, rejects;
static uint64_t now, exchange_ticks, scheduler_pause;
static int failure;
static bool faulted, stop_on_poll;
static uint8_t expected[8];
uint64_t pbl_uptime_ticks(void) { return now; }
void pbl_thread_sleep(pbl_timeout_t timeout) {
  assert(timeout.ticks && timeout.ticks<=pbl_ms_to_ticks(T5AI_HCI_ACL_RETRY_DELAY_MS));
  ++sleeps;now+=timeout.ticks+scheduler_pause;
}
void ble_hs_sched_reset(int reason) { assert(reason==BLE_HS_ECONTROLLER);++resets; }
void pbl_log(uint8_t level,const char *file,int line,const char *fmt,...) {
  (void)file;(void)line;(void)fmt;if(level==LOG_LEVEL_WARNING)++warnings;
}
void pbl_log_filtered(uint8_t level,const char *file,int line,const char *fmt,...) {
  (void)file;(void)line;(void)fmt;if(level==LOG_LEVEL_WARNING)++warnings;
}
// Endpoints retained by ASan's globals even though these task/RX paths are
// outside this TX test. Fail visibly if a scenario unexpectedly enters them.
static unsigned locks;
int pbl_mutex_lock_lr(struct pbl_mutex *m,pbl_timeout_t t,uintptr_t lr) {
  (void)t;(void)lr;assert(m==&s_tx_mutex && !locks);++locks;return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(m==&s_tx_mutex && locks==1);--locks; }
int pbl_msgq_put(struct pbl_msgq *q,const void *msg,pbl_timeout_t t) { assert(false);return -1; }
void pbl_sem_give(struct pbl_sem *sem) { assert(false); }
int pbl_msgq_get(struct pbl_msgq *q,void *msg,pbl_timeout_t t) { assert(false);return -1; }
int pbl_sem_take(struct pbl_sem *sem,pbl_timeout_t t) { assert(false);return -1; }
void *ble_transport_alloc_evt(int discardable) { assert(false);return NULL; }
struct os_mbuf *ble_transport_alloc_acl_from_ll(void) { assert(false);return NULL; }
void ble_transport_free(void *buf) { assert(false); }
int ble_transport_to_hs_evt_impl(void *buf) { assert(false);return -1; }
int ble_transport_to_hs_acl_impl(struct os_mbuf *buf) { assert(false);return -1; }
bool t5ai_hci_ipc_sleep_ready(void) { assert(false);return false; }
void *os_memblock_get(struct os_mempool *mp) { assert(false);return NULL; }
os_error_t os_memblock_put(struct os_mempool *mp,void *block) {
  assert(mp==&mempool);
  for(unsigned i=0;i<2;++i) if(block==&packets[i].mbuf){assert(!frees[i]);++frees[i];return 0;}
  assert(false);return 0;
}
int t5ai_hci_ipc_send_acl(const uint8_t *data,size_t length) {
  assert(data==s_acl_tx && length==sizeof expected && !memcmp(data,expected,length));
  assert(!frees[0] && !frees[1]);++sends;now+=exchange_ticks;
  return sends<=rejects?failure:0;
}
bool t5ai_hci_ipc_faulted(void) { return faulted; }
void t5ai_hci_ipc_poll(void) { ++polls;if(stop_on_poll)prv_set_started(false); }
static void setup(uint64_t start) {
  memset(packets,0,sizeof packets);memset(frees,0,sizeof frees);
  sends=polls=resets=warnings=sleeps=0;rejects=0;failure=E_ERROR;
  faulted=stop_on_poll=false;exchange_ticks=scheduler_pause=0;now=start;
  s_reset_scheduled=false;prv_set_started(true);
  uint8_t bytes[]={1,0,4,0,0xaa,0xbb,0xcc,0xdd};memcpy(expected,bytes,sizeof bytes);
  for(unsigned i=0;i<2;++i) {
    packets[i].mbuf.om_data=packets[i].data;packets[i].mbuf.om_omp=&pool;
    packets[i].mbuf.om_pkthdr_len=sizeof(struct os_mbuf_pkthdr);
    packets[i].mbuf.om_len=sizeof bytes;OS_MBUF_PKTLEN(&packets[i].mbuf)=sizeof bytes;
    memcpy(packets[i].data,bytes,sizeof bytes);
  }
}
int main(void) {
  // Real NimBLE copy/free traverses a fragmented chain exactly once.
  setup(0);packets[0].mbuf.om_len=3;
  packets[1].mbuf.om_len=5;packets[1].mbuf.om_pkthdr_len=0;
  memcpy(packets[1].data,expected+3,5);
  SLIST_NEXT(&packets[0].mbuf,om_next)=&packets[1].mbuf;
  rejects=3;prv_send_queued_acl(&packets[0].mbuf);
  assert(sends==4 && frees[0]==1 && frees[1]==1 && !resets && warnings==1);
  // A permanently rejecting controller expires once, including beyond 32-bit uptime.
  const uint64_t starts[]={0,UINT32_MAX-2ull,(uint64_t)UINT32_MAX+1000ull};
  for(unsigned i=0;i<3;++i) {
    setup(starts[i]);rejects=UINT32_MAX;prv_send_queued_acl(&packets[0].mbuf);
    assert(now-starts[i]==pbl_ms_to_ticks(T5AI_HCI_ACL_PACKET_TIMEOUT_MS));
    assert(sends==500 && sleeps==500 && warnings==3 && resets==1 && frees[0]==1);
    assert(!prv_started() && s_reset_scheduled && !frees[1]);
  }
  // Unpublished backpressure is retryable; terminal/ambiguous failures are not replayed.
  setup(0);failure=E_BUSY;rejects=2;prv_send_queued_acl(&packets[0].mbuf);
  assert(sends==3 && !resets && frees[0]==1);
  const int terminal[]={E_AGAIN,E_INVALID_OPERATION,E_OUT_OF_RESOURCES,E_RANGE,
                       E_INVALID_ARGUMENT,E_OUT_OF_MEMORY,E_INTERNAL};
  for(unsigned i=0;i<sizeof terminal/sizeof terminal[0];++i) {
    setup(0);rejects=1;failure=terminal[i];prv_send_queued_acl(&packets[0].mbuf);
    assert(sends==1 && !sleeps && resets==1 && frees[0]==1);
  }
  setup(0);rejects=1;faulted=true;prv_send_queued_acl(&packets[0].mbuf);
  assert(sends==1 && resets==1 && frees[0]==1);
  // Old-session queued TX is consumed during reset without publishing new bytes
  // over the staging buffer still possibly referenced by a late descriptor ACK.
  assert(ble_transport_to_ll_acl_impl(&packets[1].mbuf)==0);
  assert(sends==1 && frees[1]==1 && !locks && !memcmp(s_acl_tx,expected,sizeof expected));
  // Neither scheduler delays nor a blocking exchange restart the packet deadline.
  setup(0);rejects=UINT32_MAX;scheduler_pause=pbl_ms_to_ticks(3000);
  prv_send_queued_acl(&packets[0].mbuf);assert(sends==1 && resets==1 && frees[0]==1);
  setup(0);rejects=UINT32_MAX;exchange_ticks=pbl_ms_to_ticks(2000);
  prv_send_queued_acl(&packets[0].mbuf);assert(sends==2 && resets==1 && frees[0]==1);
  setup(0);rejects=1;stop_on_poll=true;prv_send_queued_acl(&packets[0].mbuf);
  assert(sends==1 && !resets && !sleeps && frees[0]==1);
  setup(0);packets[0].data[2]=5;prv_send_queued_acl(&packets[0].mbuf);
  assert(!sends && frees[0]==1 && !resets);
  return 0;
}
"""


class HciAclTests(unittest.TestCase):
    def test_deadline_statuses_fragment_ownership_and_long_uptime(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            config = load_config("unhygienix")
            config.syms["LOG_HASHED"].set_value("n")
            config.write_autoconf(str(path / "config.h"))
            (path / "test.c").write_text(HARNESS)
            nimble = ROOT / "third_party/nimble"
            includes = [
                ROOT,
                ROOT / "include",
                ROOT / "include/pbl",
                ROOT / "pebble",
                ROOT / "kernel/arch/posix/include",
                nimble / "port/include",
                nimble / "mynewt-nimble/porting/nimble/include",
                nimble / "mynewt-nimble/nimble/include",
                nimble / "mynewt-nimble/nimble/host/include",
                nimble / "mynewt-nimble/nimble/transport/include",
            ]
            subprocess.run(
                [
                    "clang-21",
                    "-std=gnu23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-pie",
                    "-no-pie",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-Wl,--gc-sections",
                    "-include",
                    str(path / "config.h"),
                    *[arg for include in includes for arg in ("-I", str(include))],
                    str(path / "test.c"),
                    str(nimble / "mynewt-nimble/porting/nimble/src/os_mbuf.c"),
                    str(ROOT / "kernel/tick.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
