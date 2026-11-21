# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the complete CP console with real UART, work and mailbox interfaces."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    "pbl/kernel/irq.h": "#pragma once\nvoid pbl_irq_lock(void);\nvoid pbl_irq_unlock(void);\nbool pbl_irq_is_locked(void);\n",
    "pbl/mcu/interrupts.h": "bool mcu_state_is_isr(void);\n",
}
HARNESS = r"""
#include <assert.h>
#include <string.h>
#include "drivers/uart/t5ai_console.c"
static unsigned depth, submissions, calls, unrelated, lock_calls;
static bool running, owner, isr, sched_locked, produce, fail, reenter;
static uint8_t output[16384];
static size_t written;
struct pbl_workq pbl_sys_workq;
const struct pbl_device_class pbl_uart_class = {.name="uart"};
int pbl_uart_init(const struct pbl_device *dev) { return 0; }
bool pbl_kernel_is_running(void) { return running; }
bool t5ai_mailbox_channel_is_owner(void) { return owner; }
bool mcu_state_is_isr(void) { return isr; }
bool pbl_irq_is_locked(void) { return depth != 0; }
bool pbl_sched_is_locked(void) { return sched_locked; }
void pbl_irq_lock(void) { ++depth; }
void pbl_irq_unlock(void) { assert(depth); --depth; }
void __real_pbl_console_lock(void) { ++lock_calls; }
void __real_pbl_console_unlock(void) { assert(lock_calls); --lock_calls; }
// FIFO endpoint matching the kernel's documented coalescing/resubmit contract.
bool pbl_work_submit(struct pbl_workq *q, struct pbl_work *work) {
  assert(q == &pbl_sys_workq && work && work->fn); ++submissions;
  if (work->pending) return false;
  work->pending=true;work->next=NULL;
  if (q->tail) q->tail->next=work; else q->head=work;
  q->tail=work;return true;
}
static void next(void) {
  struct pbl_workq *q=&pbl_sys_workq;
  struct pbl_work *w=q->head;assert(w);q->head=w->next;
  if (!q->head) q->tail=NULL;
  w->pending=false;w->fn(w);
}
static void write_bytes(size_t count) {
  pbl_irq_lock();
  for (size_t i=0;i<count;++i) prv_poll_out(NULL, (uint8_t)i);
  prv_flush(NULL);pbl_irq_unlock();
}
status_t t5ai_cp_mailbox_log(const void *data, size_t length) {
  assert(!depth && !owner && !isr && length && length<=159);++calls;
  assert(written+length<=sizeof output);
  if (reenter) prv_panic(NULL); // Concurrent/reentrant drain never interleaves IPC.
  if (produce) write_bytes(length);
  if (fail) return E_BUSY;
  memcpy(output+written,data,length);written+=length;return S_SUCCESS;
}
static void other(struct pbl_work *w) { ++unrelated; }
static PBL_WORK_DEFINE(other_work, other);
int main(void) {
  write_bytes(2);assert(!written && !submissions);
  running=true;prv_flush(NULL);next();assert(written==2 && !s_length);
  for (unsigned pass=0;pass<4;++pass) {
    size_t start=written;write_bytes(500);next();
    assert(written==start+500 && !s_length);
    for (size_t i=0;i<500;++i) assert(output[start+i]==(uint8_t)i);
  }
  owner=true;write_bytes(1);__wrap_pbl_console_lock();__wrap_pbl_console_unlock();
  assert(!s_length && !lock_calls);owner=false;
  __wrap_pbl_console_lock();__wrap_pbl_console_unlock();assert(!lock_calls);
  write_bytes(sizeof s_buffer+31);assert(s_overflow_drops==31 && s_length==sizeof s_buffer);
  size_t start=written;next();assert(written-start==CONSOLE_WORK_BYTE_BUDGET);
  assert(s_send.pending && s_length==sizeof s_buffer-CONSOLE_WORK_BYTE_BUDGET);
  next();assert(!s_length);
  for(size_t i=0;i<sizeof s_buffer;++i) assert(output[start+i]==(uint8_t)i);
  // Continuous producers resubmit at the tail, allowing unrelated system work.
  write_bytes(500);pbl_work_submit(&pbl_sys_workq,&other_work);produce=true;
  unsigned before=calls;next();assert(calls-before==4 && s_length==500);
  next();assert(unrelated==1 && s_length==500);
  produce=false;next();assert(!s_length);
  write_bytes(500);fail=true;before=calls;next();fail=false;
  assert(s_send_failures==calls-before && s_send_failures==4 && !s_length);
  // Panic skips unsafe contexts, then sends at most one capacity without resubmission.
  write_bytes(sizeof s_buffer);unsigned submitted=submissions;
  pbl_irq_lock();prv_panic(NULL);pbl_irq_unlock();assert(s_length==sizeof s_buffer);
  isr=true;prv_panic(NULL);isr=false;assert(s_length==sizeof s_buffer);
  sched_locked=true;prv_panic(NULL);sched_locked=false;assert(s_length==sizeof s_buffer);
  owner=true;prv_panic(NULL);owner=false;assert(s_length==sizeof s_buffer);
  running=false;prv_panic(NULL);running=true;assert(s_length==sizeof s_buffer);
  start=written;produce=true;reenter=true;prv_panic(NULL);
  assert(written-start==sizeof s_buffer && s_length==sizeof s_buffer);
  // Producer flushes coalesce; the panic handler itself adds no submission.
  assert(submissions==submitted+7);
  produce=reenter=false;while(pbl_sys_workq.head) next();assert(!s_length && !depth);
  struct pbl_uart_config cfg={.baudrate=460800};
  assert(s_ops.configure(&T5AI_CONSOLE_UART,&cfg)==0);
  cfg.baudrate=115200;assert(s_ops.configure(&T5AI_CONSOLE_UART,&cfg)==-ENOTSUP);
  return 0;
}
"""


class ConsoleTests(unittest.TestCase):
    def test_bounded_work_order_overflow_failure_and_panic(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            for name, content in HEADERS.items():
                header = path / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(content)
            (path / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "clang-21",
                    "-std=gnu23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-DCONFIG_KERNEL_THREAD_TLS_SLOTS=2",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-pie",
                    "-no-pie",
                    "-I",
                    str(path),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                    "-I",
                    str(ROOT / "include/pbl"),
                    "-I",
                    str(ROOT / "pebble"),
                    "-I",
                    str(ROOT / "kernel/arch/posix/include"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
