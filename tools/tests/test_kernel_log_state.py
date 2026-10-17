# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual kernel log-state paths for registered and native threads."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class KernelLogStateTests(unittest.TestCase):
    def test_native_thread_log_state(self):
        p = ROOT / "fw/kernel/kernel_applib_state.c"
        s = p.read_text()

        def fun(name):
            at = s.index(name + "(")
            start = s.rfind("\n", 0, at) + 1
            brace = s.index("{", at)
            depth = 1
            end = brace + 1
            while depth:
                depth += (s[end] == "{") - (s[end] == "}")
                end += 1
            return s[start:end]

        statics = "\n".join(
            x
            for x in s.splitlines()
            if x.startswith("static ") and ("s_log_state_" in x)
        )
        h = """#include <assert.h>
        #include <stdbool.h>
        #include <stddef.h>
        #include <stdint.h>
        #define nullptr NULL
        #define PBL_MUTEX_DEFINE(name) int name
        #define PBL_MSEC(x) (x)
        @TASK_ENUM@
        typedef struct { bool in_progress; char buffer[128]; } LogState;
        static PebbleTask task;
        static unsigned locks,unlocks;
        static void *pebble_task_get_thread(PebbleTask t) {(void)t;return (void *)1;}
        static PebbleTask pebble_task_get_current(void) {return task;}
        static bool mcu_state_is_isr(void) {return false;}
        static bool pbl_irq_is_locked(void) {return false;}
        static bool pbl_kernel_is_running(void) {return true;}
        static int pbl_mutex_lock(int *m,int timeout) {(void)m;(void)timeout;++locks;return 0;}
        static void pbl_mutex_unlock(int *m) {(void)m;++unlocks;}
        static void dbgserial_putstr(const char *s) {(void)s;}
        """
        enum_source = (ROOT / "fw/kernel/pebble_tasks.h").read_text()
        enum_start = enum_source.index("typedef enum PebbleTask")
        enum_end = enum_source.index("} PebbleTask;", enum_start) + len("} PebbleTask;")
        h = h.replace("@TASK_ENUM@", enum_source[enum_start:enum_end])
        tail = """int main(void) {
         s_log_state_mutex_ready=true;
         for (unsigned t=0;t<=PebbleTask_Unknown;++t) {
          if(t==NumPebbleTask) continue;
          task=t;LogState *state=kernel_applib_get_log_state();assert(state && state->in_progress);
          assert(kernel_applib_get_log_state()==NULL);
          kernel_applib_release_log_state(state);assert(!state->in_progress);
         }
         assert(locks==unlocks);return 0;
        }"""
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / "test.c"
            exe = Path(tmp) / "test"
            c.write_text(
                h
                + statics
                + "\n"
                + fun("prv_get_current_task")
                + "\n"
                + fun("kernel_applib_get_log_state")
                + "\n"
                + fun("kernel_applib_release_log_state")
                + "\n"
                + tail
            )
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-g",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    str(c),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run([str(exe)], check=True)
