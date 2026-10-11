# SPDX-License-Identifier: Apache-2.0
"""Exercise actual recovery wrapper delegation and priority snapshots."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.tests.test_t5ai_bg_pick_trace import STUB as BASE_STUB

ROOT = Path(__file__).resolve().parents[2]
STUB = BASE_STUB.replace("uint32_t state,run_time,switched_in_at;", "uint32_t state,run_time,switched_in_at,base_prio,mutexes_held;void *waitq;") + r"""
typedef uint32_t pbl_tick_t;
typedef void *(*pbl_task_wdt_callback_t)(int,void*);
typedef int status_t;
typedef struct {uint32_t count,max_ms,last_ms,last_tick,last_offset,last_length;} T5AIFlashOperationDiagnostics;
typedef struct {T5AIFlashOperationDiagnostics read,write,erase;uint32_t active_command,active_tick;} T5AIFlashDiagnostics;
void t5ai_flash_ipc_get_diagnostics(T5AIFlashDiagnostics*);
void log_snapshot(const char*,unsigned,...);
#define PBL_LOG_SYNC_WRN log_snapshot
"""
HARNESS = r"""
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include SOURCE
struct pbl_thread bg={.id=7,.prio=1},app={.id=8,.prio=3};
struct pbl_thread *pbl_cur=&bg,*g_task_threads[2]={&bg,&app};
static pbl_task_wdt_callback_t registered;
static void *registered_data;
static int result=2, calls, logs, locked;
void pbl_irq_lock(void){++locked;}
void pbl_irq_unlock(void){--locked;}
uint32_t pbl_uptime_ticks(void){return 123;}
void log_snapshot(const char *fmt,unsigned phase,...){
 assert(!locked);++logs;
 va_list ap;va_start(ap,phase);
 if(strstr(fmt,"app phase")){
  assert(va_arg(ap,uint32_t)==8);assert(va_arg(ap,uint32_t)==(phase?0:2));
  assert(va_arg(ap,uint32_t)==3);assert(va_arg(ap,uint32_t)==1);
 }else if(strstr(fmt,"clocks")){
  assert(va_arg(ap,uint32_t)==123);
 }
 va_end(ap);
}

int __real_pbl_task_wdt_add(struct pbl_thread *thread,uint32_t ms,
                           pbl_task_wdt_callback_t cb,void *data){
 assert(ms==5000);registered=cb;registered_data=data;return result;
}
void t5ai_flash_ipc_get_diagnostics(T5AIFlashDiagnostics *d){
 assert(!locked);*d=(T5AIFlashDiagnostics){.read={.count=12,.max_ms=8}};
}
status_t __real_settings_blob_db_mark_all_dirty(void){
 assert(s_settings_active==1&&s_settings_start==123);return -7;
}
static void *callback(int channel,void *data){
 assert(channel==2);assert(data==(void*)0x1234);++calls;
 app.backend.base_prio=0;return (void*)0x5678;
}
int main(void){
 app.backend.base_prio=2;app.backend.mutexes_held=1;
 assert(__wrap_pbl_task_wdt_add(&app,5000,callback,(void*)0x1234)==2);
 assert(registered==callback&&registered_data==(void*)0x1234);
 result=-12;assert(__wrap_pbl_task_wdt_add(NULL,5000,callback,(void*)0x1234)==-12);
 assert(!s_callback);
 result=2;assert(__wrap_pbl_task_wdt_add(NULL,5000,callback,(void*)0x1234)==2);
 assert(registered!=callback&&registered_data==NULL);
 assert(registered(2,registered_data)==(void*)0x5678);
 assert(calls==1&&logs==10&&!locked);
 assert(__wrap_settings_blob_db_mark_all_dirty()==-7&&!s_settings_active);
}
"""


class RecoveryTraceTest(unittest.TestCase):
    def test_delegation_and_snapshots(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for header in ("pbl/kernel/irq.h", "pbl/kernel/sched.h", "pbl/kernel/thread.h", "pbl/logging/logging.h",
                           "pbl/services/system_task.h", "pbl/task_wdt/task_wdt.h", "pbl/soc/t5ai/flash_ipc.h"):
                path = directory / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            (directory / "stub.h").write_text('#pragma once\n' + STUB)
            (directory / "test.c").write_text(HARNESS)
            exe = directory / "test"
            subprocess.run(["cc", "-std=c11", "-I" + str(directory),
                            '-DSOURCE="' + str(ROOT / "soc/t5ai/wdt_recovery_trace.c") + '"',
                            str(directory / "test.c"), "-o", str(exe)], check=True, capture_output=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
