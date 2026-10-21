# SPDX-License-Identifier: Apache-2.0
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r"""
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
struct pbl_thread {struct {uint32_t run_time,switched_in_at;} backend;uint32_t id;};
static struct pbl_thread idle,app,main_thread,bg;
struct pbl_thread *pbl_cur=&bg;
static uint32_t now;static int locked,logged;
#define PebbleTask_App 1
#define PebbleTask_KernelMain 2
#define PBL_TICK_HZ 1000u
static struct pbl_thread *pebble_task_get_thread(int task){return task==1?&app:&main_thread;}
static struct pbl_thread *sched_idle_thread(void){return &idle;}
static uint32_t pbl_uptime_ticks(void){return now;}
static void pbl_irq_lock(void){assert(!locked);locked=1;}
static void pbl_irq_unlock(void){assert(locked);locked=0;}
#define PBL_LOG_DBG(...) do {assert(!locked);++logged;} while(0)
"""
MAIN = r"""int main(void){
 now=UINT32_MAX-10u;idle.backend.run_time=3;app.backend.run_time=7;app.id=1;
 t5ai_io_profile_contact(false);assert(!s_cpu_started);
 t5ai_io_profile_contact(true);assert(s_cpu_started && !s_cpu_finished);
 now+=30;idle.backend.run_time+=5;app.backend.run_time+=17;
 t5ai_io_profile_contact(true);assert(s_cpu_first.wall==UINT32_MAX-10u);
 t5ai_io_profile_contact(false);assert(s_cpu_last.wall-s_cpu_first.wall==30u);
 assert(s_cpu_last.app-s_cpu_first.app==17u && s_cpu_last.idle-s_cpu_first.idle==5u);
 now+=3000;prv_cpu_dump();assert(logged==1 && !s_cpu_started);
 prv_cpu_dump();assert(logged==1);
 // Current-task runtime must include its live timeslice.
 now=100;app.backend.run_time=20;app.backend.switched_in_at=95;pbl_cur=&app;
 assert(prv_runtime(&app,now)==25 && !prv_runtime(0,now));
}
"""


class CpuProbeTest(unittest.TestCase):
    def test_contacts_wrap_runtime_and_quiet_delay(self):
        source = (ROOT / "soc/t5ai/io_profile.c").read_text()
        source = source[source.index("/* This code") : source.rindex("#endif")]
        source = re.sub(r"^#include.*\n", "", source, flags=re.MULTILINE)
        with tempfile.TemporaryDirectory() as temporary:
            p = Path(temporary)
            (p / "test.c").write_text(PREFIX + source + MAIN)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Wno-unused-variable",
                    "-fsanitize=undefined",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
