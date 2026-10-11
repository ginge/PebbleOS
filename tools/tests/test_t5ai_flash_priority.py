# SPDX-License-Identifier: Apache-2.0
"""Compile the driver's actual priority-scope helpers with an external throttle."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <stdint.h>
typedef uint8_t pbl_prio_t;
struct pbl_thread {int unused;};
#define MB_IPC_CLIENT_PRIORITY 3
static struct pbl_thread thread;
static int locks;
static pbl_prio_t priority;
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){--locks;}
static void *pbl_thread_current(void){return &thread;}
static pbl_prio_t pbl_thread_prio_get(void *t){assert(t==&thread);return priority;}
static void pbl_thread_prio_set(void *t,pbl_prio_t p){assert(t==&thread);priority=p;}
HELPERS
int main(void){
 priority=1;pbl_prio_t old=prv_boost_client_priority();assert(priority==3);
 prv_restore_client_priority(old);assert(priority==1);
 priority=2;old=prv_boost_client_priority();
 pbl_prio_t nested=prv_boost_client_priority();
 prv_restore_client_priority(nested);assert(priority==3);
 prv_restore_client_priority(old);assert(priority==2);
 priority=2;old=prv_boost_client_priority();priority=0;
 prv_restore_client_priority(old);assert(priority==0);
 priority=1;old=prv_boost_client_priority();priority=5;
 prv_restore_client_priority(old);assert(priority==5);
 priority=4;old=prv_boost_client_priority();assert(priority==4);
 prv_restore_client_priority(old);assert(priority==4);
 assert(!locks);
}
"""


class FlashPriorityTest(unittest.TestCase):
    def test_scope_preserves_external_priority_changes(self):
        source = (ROOT / "fw/drivers/flash/t5ai_flash_ipc.c").read_text()
        start = source.index("static pbl_prio_t prv_boost_client_priority(void)")
        end = source.index("#if UINTPTR_MAX", start)
        with tempfile.TemporaryDirectory() as tmp:
            cfile = Path(tmp) / "test.c"
            exe = Path(tmp) / "test"
            cfile.write_text(HARNESS.replace("HELPERS", source[start:end]))
            subprocess.run(["cc", "-std=c11", str(cfile), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
