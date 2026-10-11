# SPDX-License-Identifier: Apache-2.0
"""Compare actual T5 SDK fast paths with stock answers across process lifecycles."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUB = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define KERNEL_READONLY_DATA __attribute__((section(".kernel_unpriv_ro_bss")))
typedef enum {PebbleTask_App, PebbleTask_Worker, PebbleTask_KernelMain, NumPebbleTask} PebbleTask;
typedef enum {ProcessAppSDKType_System, ProcessAppSDKType_Legacy2x,
 ProcessAppSDKType_Legacy3x, ProcessAppSDKType_4x} ProcessAppSDKType;
typedef struct {ProcessAppSDKType type;} PebbleProcessMd;
typedef struct {const PebbleProcessMd *app_md;} ProcessContext;
struct pbl_thread {int id;};
ProcessContext *app_manager_get_task_context(void);
ProcessContext *worker_manager_get_task_context(void);
ProcessAppSDKType process_metadata_get_app_sdk_type(const PebbleProcessMd *);
"""
HARNESS = r"""
#include <assert.h>
#include SOURCE
static struct pbl_thread app,worker,kernel;
struct pbl_thread *pbl_cur;
struct pbl_thread *g_task_threads[NumPebbleTask];
static PebbleProcessMd md[2];
static ProcessContext contexts[2]={{&md[0]},{&md[1]}};
static unsigned metadata_calls,slow_calls,setups,cleanups;
ProcessContext *app_manager_get_task_context(void) {return &contexts[0];}
ProcessContext *worker_manager_get_task_context(void) {return &contexts[1];}
ProcessAppSDKType process_metadata_get_app_sdk_type(const PebbleProcessMd *m) {
 ++metadata_calls;return m->type;
}
static bool stock(unsigned sdk) {
 if(!pbl_cur)return false;
 for(unsigned i=0;i<2;++i)if(pbl_cur==g_task_threads[i])return md[i].type==sdk;
 return false;
}
bool __real_process_manager_compiled_with_legacy2_sdk(void) {++slow_calls;return stock(ProcessAppSDKType_Legacy2x);}
bool __real_process_manager_compiled_with_legacy3_sdk(void) {++slow_calls;return stock(ProcessAppSDKType_Legacy3x);}
void __real_process_manager_process_setup(PebbleTask t) {(void)t;++setups;}
void __real_process_manager_process_cleanup(PebbleTask t) {
 // Cleanup can still query the old process before unregistering it.
 pbl_cur=g_task_threads[t];
 assert(__wrap_process_manager_compiled_with_legacy2_sdk()==stock(ProcessAppSDKType_Legacy2x));
 assert(__wrap_process_manager_compiled_with_legacy3_sdk()==stock(ProcessAppSDKType_Legacy3x));
 g_task_threads[t]=0;++cleanups;
}
static void check(void) {
 assert(__wrap_process_manager_compiled_with_legacy2_sdk()==stock(ProcessAppSDKType_Legacy2x));
 assert(__wrap_process_manager_compiled_with_legacy3_sdk()==stock(ProcessAppSDKType_Legacy3x));
}
int main(void) {
 pbl_cur=0;check();pbl_cur=&kernel;check();assert(!slow_calls);
 // Before lifecycle setup, retain the stock syscall fallback.
 g_task_threads[0]=&app;pbl_cur=&app;md[0].type=ProcessAppSDKType_Legacy2x;
 check();assert(slow_calls==2);
 for(unsigned a=0;a<4;++a)for(unsigned w=0;w<4;++w) {
  md[0].type=a;md[1].type=w;
  __wrap_process_manager_process_setup(PebbleTask_App);
  __wrap_process_manager_process_setup(PebbleTask_Worker);
  g_task_threads[0]=&app;g_task_threads[1]=&worker;
  unsigned calls=slow_calls,metadata=metadata_calls;
  for(unsigned n=0;n<1000;++n) {
   pbl_cur=&app;check();pbl_cur=&worker;check();pbl_cur=&kernel;check();pbl_cur=0;check();
  }
  assert(slow_calls==calls && metadata_calls==metadata);
  __wrap_process_manager_process_cleanup(PebbleTask_App);
  pbl_cur=&worker;check(); // App cleanup must not invalidate Worker.
  __wrap_process_manager_process_cleanup(PebbleTask_Worker);
  assert(!s_app_sdk && !s_worker_sdk);
 }
 assert(setups==32 && cleanups==32 && metadata_calls==32);
 // Reusing the same thread address must use the newly launched SDK.
 md[0].type=ProcessAppSDKType_Legacy3x;
 __wrap_process_manager_process_setup(PebbleTask_App);
 g_task_threads[0]=&app;pbl_cur=&app;check();assert(!__wrap_process_manager_compiled_with_legacy2_sdk());
 return 0;
}
"""


class SdkCompatTest(unittest.TestCase):
    def test_real_fast_paths_and_readonly_storage(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            for header in ("kernel/memory_layout.h", "kernel/pebble_tasks.h",
                           "process_management/app_manager.h", "process_management/worker_manager.h",
                           "process_management/process_manager.h", "process_management/pebble_process_md.h",
                           "pbl/kernel/thread.h"):
                p = d / header
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "stub.h"\n')
            (d / "stub.h").write_text(STUB)
            (d / "test.c").write_text(HARNESS)
            executable = d / "test"
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            '-DSOURCE="' + str(ROOT / "soc/t5ai/sdk_compat.c") + '"',
                            "-I", str(d), str(d / "test.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            sections = subprocess.check_output(["objdump", "-h", str(executable)], text=True)
            self.assertRegex(sections, r"\.kernel_unpriv_ro_bss\s+00000008\b")


if __name__ == "__main__":
    unittest.main()
