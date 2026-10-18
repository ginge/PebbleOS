# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HEADERS = {
    "pbl/kernel/compiler.h": "#define PBL_FORMAT_PRINTF(a, b)\n",
    "pbl/mcu/interrupts.h": "#include <stdbool.h>\nbool mcu_state_is_isr(void);\n",
    "pbl/kernel/irq.h": "#include <stdbool.h>\nbool pbl_irq_is_locked(void);\n",
    "pbl/kernel/sched.h": """
#include <stdbool.h>
bool pbl_kernel_is_started(void);
bool pbl_kernel_is_running(void);
bool pbl_sched_is_locked(void);
""",
    "pbl/kernel/mutex.h": """
struct pbl_mutex { int unused; };
#define PBL_MUTEX_DEFINE(name) struct pbl_mutex name
#define PBL_FOREVER 0
int pbl_mutex_lock(struct pbl_mutex *, int);
void pbl_mutex_unlock(struct pbl_mutex *);
""",
    "pbl/soc/t5ai/cp_mailbox.h": """
#include <stddef.h>
int t5ai_cp_mailbox_log(const void *, size_t);
""",
}

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "console/dbgserial.h"
#include "pbl/kernel/mutex.h"

static bool started = true, irq_locked, sched_locked, isr;
static unsigned locks, sends;
static char output[512];
static size_t output_length;
bool mcu_state_is_isr(void) { return isr; }
bool pbl_kernel_is_started(void) { return started; }
bool pbl_kernel_is_running(void) { return started && !sched_locked; }
bool pbl_irq_is_locked(void) { return irq_locked; }
bool pbl_sched_is_locked(void) { return sched_locked; }
int pbl_mutex_lock(struct pbl_mutex *mutex, int timeout) {
  (void)mutex;
  (void)timeout;
  assert(!irq_locked && !sched_locked && !isr);
  locks++;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *mutex) { (void)mutex; }
int t5ai_cp_mailbox_log(const void *data, size_t length) {
  assert(!isr && !(started && (irq_locked || sched_locked)));
  assert(output_length + length <= sizeof(output));
  memcpy(output + output_length, data, length);
  output_length += length;
  sends++;
  return 0;
}

int main(void) {
  dbgserial_init();
  dbgserial_putstr("normal");
  assert(locks == 1 && sends == 1);
  assert(output_length == 8 && memcmp(output, "normal\r\n", 8) == 0);

  for (unsigned context = 0; context < 3; context++) {
    output_length = 0;
    dbgserial_putchar_lazy('A');
    unsigned previous_locks = locks, previous_sends = sends;
    irq_locked = context == 0;
    sched_locked = context == 1;
    isr = context == 2;
    dbgserial_putchar_lazy('x');
    dbgserial_putchar('x');
    dbgserial_putstr("must be dropped");
    char buffer[32];
    dbgserial_putstr_fmt(buffer, sizeof(buffer), "drop %u", context);
    dbgserial_flush();
    assert(locks == previous_locks && sends == previous_sends);
    irq_locked = sched_locked = isr = false;
    dbgserial_flush();
    assert(output_length == 1 && output[0] == 'A');
  }

  started = false;
  irq_locked = true;
  output_length = 0;
  unsigned previous_locks = locks;
  dbgserial_putstr("boot");
  assert(locks == previous_locks);
  assert(output_length == 6 && memcmp(output, "boot\r\n", 6) == 0);
  puts("PASS: runtime, IRQ lock, scheduler lock, ISR, partial buffer, bootstrap");
  return 0;
}
"""


class T5AIDbgserialTest(unittest.TestCase):
    def test_output_contexts(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in HEADERS.items():
                header = directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(contents)
            harness = directory / "probe.c"
            harness.write_text(HARNESS)
            executable = directory / "probe"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Dvsniprintf=vsnprintf", "-I", str(directory),
                    "-I", str(ROOT / "fw"),
                    str(ROOT / "fw/console/dbgserial_t5ai.c"), str(harness),
                    "-o", str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
