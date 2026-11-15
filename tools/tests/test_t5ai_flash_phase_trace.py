# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise preparation/lock wrapper attribution using both independent clocks."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int status_t;
typedef int pbl_timeout_t;
struct pbl_thread {unsigned id;};
struct pbl_mutex {struct pbl_thread *owner;};
extern uint32_t ticks,clock_aon;
#define T5AI_AON_RTC_COUNT_LOW clock_aon
#define PBL_FS_O_REPLACE 8
struct pbl_fs_file {int unused;};
struct pbl_flash {int unused;};
#define PBL_LOG_MODULE_DEFINE(...)
#define LOG_LEVEL_DEBUG 0
#define PBL_LOG_SYNC_WRN test_log
void test_log(const char*,...);
uint32_t pbl_uptime_ticks(void);
struct pbl_thread *pbl_thread_current(void);
void pbl_irq_lock(void);
void pbl_irq_unlock(void);
"""
HARNESS = r"""
#include <assert.h>
#include SOURCE
uint32_t ticks,clock_aon;
static int locked,logs;
static struct pbl_thread bg={7},app={8},*current=&bg;
struct pbl_thread *pbl_thread_current(void){return current;}
uint32_t pbl_uptime_ticks(void){return ticks;}
void pbl_irq_lock(void){++locked;}
void pbl_irq_unlock(void){assert(locked);--locked;}
void test_log(const char *fmt,...){assert(!locked);++logs;}
static void elapsed(unsigned ms){ticks+=ms;clock_aon+=32*ms;}
int __real_pbl_fs_open(struct pbl_fs_file *file,const char *n,uint32_t flags){elapsed(3);return -7;}
int __real_pbl_fs_close(struct pbl_fs_file *file){elapsed(4);return -9;}
int __real_pbl_mutex_lock_lr(struct pbl_mutex*m,pbl_timeout_t t,uintptr_t lr){elapsed(5);return -1;}
void __real_pbl_mutex_unlock(struct pbl_mutex*m){elapsed(1000);}
void __real_prefs_private_lock(void){elapsed(6);}
void __real_alerts_preferences_lock(void){elapsed(7);}
int __real_pbl_flash_read(const struct pbl_flash *f,uint32_t a,void *b,size_t s){elapsed(8);return -11;}
int __real_pbl_flash_write(const struct pbl_flash *f,uint32_t a,const void *b,size_t s){elapsed(9);return -12;}
int __real_pbl_flash_erase(const struct pbl_flash *f,uint32_t a,size_t s){elapsed(10);return -13;}
int main(void){
 ticks=clock_aon=UINT32_MAX-5;t5ai_flash_phase_trace_begin();
 assert(s_owner==7);
 struct pbl_fs_file file;
 assert(__wrap_pbl_fs_open(&file,"file",PBL_FS_O_REPLACE)==-7);
 assert(s_phases[TEMP_OPEN].count==1 && s_phases[TEMP_OPEN].aon==96);
 assert(__wrap_pbl_fs_close(&file)==-9 && s_phases[CLOSE].ticks==4);
 struct pbl_mutex m={&app};
 assert(__wrap_pbl_mutex_lock_lr(&m,0,0)==-1);
 assert(s_wait_mutex==(uintptr_t)&m && s_wait_owner==8 && s_wait_max==160);
 __wrap_pbl_mutex_unlock(&m);
 assert(s_phases[MUTEX_UNLOCK].max_aon==32000);
 __wrap_prefs_private_lock();__wrap_alerts_preferences_lock();
 assert(__wrap_pbl_flash_read(NULL,0,NULL,0)==-11);
 assert(__wrap_pbl_flash_write(NULL,0,NULL,0)==-12);
 assert(__wrap_pbl_flash_erase(NULL,0,0)==-13);
 assert(s_phases[FLASH_ERASE].ticks==10 && !logs);
 current=&app;t5ai_flash_phase_trace_begin();__wrap_pbl_fs_close(&file);
 t5ai_flash_phase_trace_end();assert(s_owner==7 && s_phases[CLOSE].count==1);
 current=&bg;t5ai_flash_phase_trace_end();assert(!s_owner && logs>0);
 __wrap_pbl_fs_close(&file);assert(s_phases[CLOSE].count==1 && !locked);
}
"""


class FlashPhaseTraceTest(unittest.TestCase):
    def test_failure_wait_preemption_owner_and_aon_wrap(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            for header in (
                "pbl/kernel/irq.h",
                "pbl/kernel/mutex.h",
                "pbl/kernel/time.h",
                "pbl/kernel/sched.h",
                "pbl/kernel/thread.h",
                "pbl/logging/logging.h",
                "pbl/fs/fs.h",
                "pbl/drivers/flash.h",
                "pbl/soc/t5ai/io_trace.h",
                "pbl/soc/t5ai/rtc.h",
            ):
                dest = p / header
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text('#include "stub.h"\n')
            (p / "stub.h").write_text(STUB)
            (p / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-I" + str(p),
                    '-DSOURCE="' + str(ROOT / "soc/t5ai/flash_phase_trace.c") + '"',
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
