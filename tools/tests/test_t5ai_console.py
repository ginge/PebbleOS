# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise IRQ-locked UART writes and CP transmission in thread context."""

import unittest
from pathlib import Path

from tools.tests.test_t5ai_native_objects import body
from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


class ConsoleTests(unittest.TestCase):
    def test_locked_writes_defer_ipc_and_panic_drains_pending_text(self):
        source = (ROOT / "drivers/uart/t5ai_console.c").read_text()
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define nullptr NULL
struct pbl_uart { int unused; };
struct pbl_log_backend { int unused; };
struct pbl_work { void (*fn)(struct pbl_work *); };
#define PBL_WORK_DEFINE(name, fn_) struct pbl_work name = {.fn=fn_}
static unsigned depth, submissions;
static bool running, owner, isr, sched_locked;
static int pbl_sys_workq;
static char output[4096];
static size_t written;
static bool pbl_kernel_is_running(void) { return running; }
static bool t5ai_mailbox_channel_is_owner(void) { return owner; }
static bool mcu_state_is_isr(void) { return isr; }
static bool pbl_irq_is_locked(void) { return depth != 0; }
static bool pbl_sched_is_locked(void) { return sched_locked; }
static void pbl_irq_lock(void) { ++depth; }
static void pbl_irq_unlock(void) { assert(depth); --depth; }
static void pbl_work_submit(int *q, struct pbl_work *work) {
  assert(q == &pbl_sys_workq && work->fn); ++submissions;
}
static int t5ai_cp_mailbox_log(const void *data, size_t length) {
  assert(!depth && !owner && !isr && length <= 159);
  assert(written + length < sizeof(output));
  memcpy(output + written, data, length); written += length; return 0;
}
"""
        harness += source[source.index("static uint8_t s_buffer") : source.index("static int prv_init")]
        harness += body(source, "prv_panic")
        harness += r"""
int main(void) {
  // Boot text survives until the system queue is available.
  pbl_irq_lock(); prv_poll_out(NULL, 'B'); prv_flush(NULL); pbl_irq_unlock();
  assert(written == 0 && submissions == 0);
  running = true;
  pbl_irq_lock(); prv_poll_out(NULL, 'A'); prv_flush(NULL); pbl_irq_unlock();
  assert(written == 0 && submissions == 1);
  s_send.fn(&s_send);
  assert(written == 2 && !memcmp(output, "BA", 2));
  // Wrap the ring repeatedly, retaining ordering and bounded CP packets.
  for (unsigned pass=0; pass<4; ++pass) {
    pbl_irq_lock();
    for (unsigned i=0; i<500; ++i) prv_poll_out(NULL, (uint8_t)i);
    prv_flush(NULL); pbl_irq_unlock(); s_send.fn(&s_send);
    for (unsigned i=0; i<500; ++i) assert((uint8_t)output[2+pass*500+i] == (uint8_t)i);
  }
  owner=true; pbl_irq_lock(); prv_poll_out(NULL,'X'); prv_flush(NULL); pbl_irq_unlock();
  assert(s_length == 0); owner=false;
  // A fatal thread can drain its last line without waiting for the work queue.
  pbl_irq_lock(); prv_poll_out(NULL,'!'); prv_flush(NULL); prv_panic(NULL); pbl_irq_unlock();
  assert(s_length == 1); isr=true; prv_panic(NULL); assert(s_length == 1);
  isr=false; sched_locked=true; prv_panic(NULL); assert(s_length == 1);
  sched_locked=false; prv_panic(NULL); assert(s_length == 0 && output[written-1]=='!');
  return 0;
}
"""
        run_c(harness, {})


if __name__ == "__main__":
    unittest.main()
