# SPDX-License-Identifier: Apache-2.0
"""Compile production recorder hooks and startup against a deterministic clock."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def fn(s, name):
    m = re.search(
        r"^(?:static )?(?:bool|uint32_t|void) " + name + r"\(", s, re.MULTILINE
    )
    a = s.index("{", m.end())
    n = 1
    b = a + 1
    while n:
        n += (s[b] == "{") - (s[b] == "}")
        b += 1
    return s[m.start() : b]


HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
static unsigned control;
#define __get_CONTROL() control
#define __get_IPSR() 0
static void pbl_irq_lock(void){}
static void pbl_irq_unlock(void){}
#define LIMIT 192u
#define DEPTH 64u
#define PBL_PRIO_IDLE 0
#define PBL_ASSERTN assert
#define PebbleTask_App 3
static uint32_t clock_ticks, kernel_ticks;
#define T5AI_AON_RTC_COUNT_LOW clock_ticks
typedef struct {uint32_t fn,count,wall,cpu;} Counter;
typedef struct {uint32_t fn,wall,cpu;} Span;
static Counter s_counts[LIMIT], s_dump[LIMIT];static Span s_spans[DEPTH];
static uint32_t s_depth,s_lost,s_last;static bool s_ready,s_frozen,s_storage_ready;
struct pbl_thread {struct {uint32_t run_time,switched_in_at;} backend;};
static struct pbl_thread app,s_worker,*pbl_cur=&app;
static uint8_t s_stack[2048];
struct pbl_thread_attr {const char *name;void (*entry)(void*);unsigned prio;bool privileged;void *stack;unsigned stack_size;};
static uint32_t pbl_uptime_ticks(void){return kernel_ticks;}
static struct pbl_thread *pebble_task_get_thread(unsigned id){assert(id==3);return &app;}
static void __real_system_task_timer_init(void){}
static int pbl_thread_create(struct pbl_thread *t,const struct pbl_thread_attr *a){(void)t;(void)a;return 0;}
static void prv_entry(void *arg){(void)arg;}
"""
MAIN = r"""
static Counter *find(uint32_t fn){for(unsigned i=0;i<LIMIT;++i)if(s_counts[i].fn==fn)return &s_counts[i];return 0;}
int main(void){
 memset(s_counts,0xaa,sizeof s_counts);memset(s_dump,0xaa,sizeof s_dump);
 __wrap_system_task_timer_init();assert(s_ready);
 control=1;assert(!prv_app());assert(s_counts[0].fn==0xaaaaaaaa);control=0;
 assert(prv_app());
 for(unsigned i=0;i<LIMIT;++i)assert(!s_counts[i].fn && !s_dump[i].fn);
 clock_ticks=100;kernel_ticks=10;
 __cyg_profile_func_enter((void*)0x1001,0);
 clock_ticks=120;kernel_ticks=11;
 __cyg_profile_func_enter((void*)0x2001,0);
 clock_ticks=160;kernel_ticks=12;
 __cyg_profile_func_exit((void*)0x2001,0);
 clock_ticks=200;kernel_ticks=15;
 __cyg_profile_func_exit((void*)0x1001,0);
 assert(find(0x2001) && find(0x2001)->wall==40 && find(0x2001)->cpu==1);
 assert(find(0x1001) && find(0x1001)->wall==100 && find(0x1001)->cpu==5);
 assert(!s_depth && !s_lost);
 for(unsigned i=0;i<DEPTH+3;++i)__cyg_profile_func_enter((void*)0x3001,0);
 for(unsigned i=0;i<DEPTH+3;++i)__cyg_profile_func_exit((void*)0x3001,0);
 assert(!s_depth && s_lost==3);
 control=1;assert(!prv_app());control=0;
 s_frozen=true;__cyg_profile_func_enter((void*)0x4001,0);assert(!s_depth);
 return 0;
}
"""


class DrawProfileTest(unittest.TestCase):
    def test_noload_initialization_nesting_and_overflow(self):
        s = (ROOT / "soc/t5ai/draw_profile.c").read_text()
        names = [
            "prv_app",
            "prv_cpu",
            "prv_add",
            "__cyg_profile_func_enter",
            "__cyg_profile_func_exit",
            "__wrap_system_task_timer_init",
        ]
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "test.c").write_text(
                HARNESS + "\n".join(fn(s, n) for n in names) + MAIN
            )
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    str(d / "test.c"),
                    "-o",
                    str(d / "test"),
                ],
                check=True,
            )
            subprocess.run([str(d / "test")], check=True)
