# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HEADERS = {
    "pbl/kernel/time.h": '#include "pbl/kernel/sched.h"\n',
    "pbl/kernel/irq.h": "void pbl_irq_lock(void);\nvoid pbl_irq_unlock(void);\n",
    "pbl/kernel/sched.h": """
#include <stdint.h>
#define PBL_TICK_HZ 1000u
#define PBL_MSEC(x) (x)
uint32_t pbl_uptime_ticks(void);
""",
    "pbl/kernel/thread.h": """
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PBL_PRIO_MAX 7
#define PBL_THREAD_STACK_DEFINE(name, size) static uint8_t name[size]
struct pbl_thread_backend {
  uint32_t run_time, switched_in_at;
  void *waitq;
  uint8_t state, base_prio, mutexes_held;
};
struct pbl_thread { struct pbl_thread_backend backend; uint32_t id; uint8_t prio; };
struct pbl_thread_attr {
  const char *name;
  void (*entry)(void *);
  unsigned prio;
  bool privileged;
  void *stack;
  size_t stack_size;
};
struct pbl_thread *pbl_thread_current(void);
void pbl_thread_sleep(unsigned ticks);
int pbl_thread_create(struct pbl_thread *, const struct pbl_thread_attr *);
""",
    "pbl/logging/logging.h": """
#define LOG_LEVEL_DEBUG 4
#define PBL_LOG_MODULE_DEFINE(a, b)
void probe_log(const char *, ...) __attribute__((format(printf, 1, 2)));
#define PBL_LOG_INFO(...) probe_log(__VA_ARGS__)
#define PBL_LOG_WRN(...) probe_log(__VA_ARGS__)
#define PBL_LOG_DBG(...) probe_log(__VA_ARGS__)
""",
    "pbl/mcu/cache.h": "#include <stddef.h>\nvoid dcache_flush(const void *, size_t);\n",
    "pbl/services/system_task.h": "void *system_task_get_current_callback(void);\n",
    "kernel/pebble_tasks.h": """
#include "pbl/kernel/thread.h"
enum { PebbleTask_KernelBackground, PebbleTask_App };
struct pbl_thread *pebble_task_get_thread(int);
""",
    "system/passert.h": "#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n",
}

HARNESS = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include T5AI_TRACE_SOURCE

static unsigned irq_depth, flushes, log_count;
static uint32_t now;
static void *callback;
static struct pbl_thread bg, app, sampler;
static char logs[64][256];
void pbl_irq_lock(void) { irq_depth++; }
void pbl_irq_unlock(void) { assert(irq_depth); irq_depth--; }
uint32_t pbl_uptime_ticks(void) { return now; }
struct pbl_thread *pbl_thread_current(void) { return &sampler; }
struct pbl_thread *pebble_task_get_thread(int task) {
  assert(irq_depth);
  return task == PebbleTask_App ? &app : &bg;
}
void *system_task_get_current_callback(void) { assert(irq_depth); return callback; }
void dcache_flush(const void *address, size_t length) {
  (void)address; assert(length); flushes++;
}
void pbl_thread_sleep(unsigned ticks) { (void)ticks; }
int pbl_thread_create(struct pbl_thread *thread, const struct pbl_thread_attr *attr) {
  (void)thread;
  assert(attr->prio > 2 && attr->prio < PBL_PRIO_MAX);
  return 0;
}
void probe_log(const char *format, ...) {
  assert(!irq_depth && log_count < 64);
  va_list args;
  va_start(args, format);
  vsnprintf(logs[log_count++], sizeof(logs[0]), format, args);
  va_end(args);
}
static void reset_trace(void) {
  memset(&s_trace, 0, sizeof(s_trace));
  s_trace.magic = TRACE_MAGIC;
  s_trace.next_sequence = 1;
  s_live = true;
  s_callback = s_callback_since = 0;
  bg = (struct pbl_thread){.id = 1, .prio = 1,
    .backend = {.base_prio = 1, .run_time = 50}};
  app = (struct pbl_thread){.id = 2, .prio = 2,
    .backend = {.base_prio = 2, .run_time = 100}};
  now = 1000;
  callback = (void *)0x1234;
  log_count = flushes = 0;
}
int main(void) {
  reset_trace();
  ThreadState missing = prv_thread_state(NULL, now);
  assert(missing.id == 0 && missing.scheduling == 255);
  sampler.backend.run_time = 10;
  sampler.backend.switched_in_at = 500;
  assert(prv_thread_state(&sampler, now).cpu_ticks == 510);
  prv_sample();
  now += 1000;
  app.backend.run_time += 1000;
  prv_sample();
  Sample *sample = &s_trace.samples[1];
  assert(sample->bg.cpu_ticks == 50 && sample->app.cpu_ticks == 1100);
  assert(sample->ticks - sample->callback_since == 1000);
  assert(sample->bg.scheduling == 0x00010100);
  assert(log_count == 0 && flushes && !irq_depth);

  bg.backend.state = 2;
  bg.backend.waitq = (void *)0x5678;
  bg.prio = 5;
  bg.backend.mutexes_held = 2;
  callback = (void *)0x2345;
  now += 1000;
  prv_sample();
  sample = &s_trace.samples[2];
  assert(sample->bg.waitq == 0x5678 && sample->bg.scheduling == 0x02050102);
  assert(sample->ticks == sample->callback_since);
  app.id = 3;
  app.backend.run_time = 0;
  now += 1000;
  prv_sample();
  assert(s_trace.samples[3].app.id == 3 && s_trace.samples[3].app.cpu_ticks == 0);

  for (unsigned i = 4; i < 20; i++) { now += 1000; prv_sample(); }
  prv_dump_previous();
  assert(log_count == 49);
  assert(strstr(logs[1], "sample=5 "));
  assert(strstr(logs[46], "sample=20 "));
  assert(strstr(logs[2], "BG id=1"));
  assert(strstr(logs[3], "APP id=3"));

  log_count = 0;
  t5ai_state_trace_freeze(7);
  unsigned frozen_sequence = s_trace.next_sequence;
  assert(s_trace.frozen && s_trace.reason == 7);
  now += 1000;
  prv_sample();
  t5ai_state_trace_freeze(8);
  assert(s_trace.next_sequence == frozen_sequence && s_trace.reason == 7);
  assert(log_count == 0 && !irq_depth);

  // Simulate a warm reset: ordinary BSS is cleared, retained RAM stays intact.
  s_live = false;
  s_callback = s_callback_since = 0;
  prv_dump_previous();
  assert(strstr(logs[0], "frozen=1 reason=7"));
  assert(strstr(logs[46], "sample=20 "));
  log_count = 0;
  s_trace.samples[4].checksum ^= 1;
  prv_dump_previous();
  assert(log_count == 47); // corrupt record is reported, not decoded
  bool invalid = false;
  for (unsigned i = 0; i < log_count; i++) {
    if (strstr(logs[i], "invalid sample=")) invalid = true;
  }
  assert(invalid);
  log_count = 0;
  s_trace.magic ^= 1;
  prv_dump_previous();
  assert(log_count == 0);
  t5ai_state_trace_start();
  puts("PASS: starvation/blocked state, priority, callback observation, ring wrap, freeze, retention, checksum");
}
"""


class T5AIStateTraceTest(unittest.TestCase):
    def test_state_recorder(self):
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
                    "-DCONFIG_T5AI_STATE_TRACE=1", "-I", str(directory),
                    "-I", str(ROOT / "include"),
                    f'-DT5AI_TRACE_SOURCE="{ROOT / "soc/t5ai/state_trace.c"}"',
                    str(harness), "-o", str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
