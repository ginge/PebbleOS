# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Actual-source T5 interrupt timing diagnostics with controlled hardware time."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include PROFILE_SOURCE
static unsigned depth, real_disables, real_enables, real_ticks, logs;
static char last_log[512];
void __real_arch_irq_disable(void) { ++real_disables; }
void __real_arch_irq_enable(void) { ++real_enables; }
void __real_SysTick_Handler(void) { ++real_ticks; T5AI_AON_RTC_COUNT_LOW += 2; }
void pbl_irq_lock(void) { __wrap_arch_irq_disable(); ++depth; }
void pbl_irq_unlock(void) { assert(depth); if (!--depth) __wrap_arch_irq_enable(); }
void probe_log(const char *fmt, ...) {
 assert(!depth); ++logs;
 va_list args; va_start(args,fmt); vsnprintf(last_log,sizeof(last_log),fmt,args); va_end(args);
}
int main(void) {
 assert(mmap((void *)0x44000000u, 0x11000, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
 CLOCK_MODE = 0x31; CPU1_CLOCK = 16; SysTick->LOAD = 239999;
 // Disabled collection still calls every original handler/mask operation.
 __wrap_arch_irq_disable(); __wrap_arch_irq_enable(); __wrap_SysTick_Handler();
 assert(real_disables == 1 && real_enables == 1 && real_ticks == 1);
 assert(!s_profile.masks && !s_profile.ticks && !logs);
 t5ai_interrupt_profile_start();
 T5AI_AON_RTC_COUNT_LOW = 100; __wrap_SysTick_Handler();
 assert(s_profile.ticks == 1 && s_profile.body_max == 2 && !s_profile.gap_max);
 T5AI_AON_RTC_COUNT_LOW = 164; CPU1_CLOCK = 0; __wrap_SysTick_Handler();
 assert(s_profile.gap_max == 64 && s_profile.gaps_1_5ms == 1 && !s_profile.gaps_3ms);
 assert(s_profile.clock_changes == 1 && !s_profile.reload_changes);
 // Unrelated clock fields do not count as core-clock changes.
 CLOCK_MODE |= 1u << 24; SysTick->LOAD = 119999;
 T5AI_AON_RTC_COUNT_LOW = 360; __wrap_SysTick_Handler();
 assert(s_profile.gap_max == 196 && s_profile.gaps_3ms == 1);
 assert(s_profile.clock_changes == 1 && s_profile.reload_changes == 1);
 t5ai_interrupt_profile_start(); assert(s_profile.ticks == 3); // Once only.
 // Nested locks measure one outermost masked interval.
 T5AI_AON_RTC_COUNT_LOW = 400; pbl_irq_lock();
 T5AI_AON_RTC_COUNT_LOW = 410; pbl_irq_lock();
 T5AI_AON_RTC_COUNT_LOW = 420; pbl_irq_unlock();
 assert(!s_profile.masks);
 T5AI_AON_RTC_COUNT_LOW = 440; pbl_irq_unlock();
 assert(s_profile.masks == 1 && s_profile.mask_total == 40 && s_profile.mask_max == 40);
 // Unsigned deltas retain counter wrap semantics.
 s_tick_previous = UINT32_MAX - 10; T5AI_AON_RTC_COUNT_LOW = 22;
 __wrap_SysTick_Handler(); assert(s_profile.gap_max == 196);
 T5AI_AON_RTC_COUNT_LOW = UINT32_MAX - 4; pbl_irq_lock();
 T5AI_AON_RTC_COUNT_LOW = 5; pbl_irq_unlock();
 assert(s_profile.mask_total == 50 && s_profile.mask_max == 40);
 T5AI_AON_RTC_COUNT_LOW = 1000;
 uint32_t start = t5ai_interrupt_profile_irq_enter();
 T5AI_AON_RTC_COUNT_LOW = 1004; t5ai_interrupt_profile_irq_exit(0, start);
 assert(s_irqs[0].count == 1 && s_irqs[0].maximum == 4 && !s_irqs[1].count);
 t5ai_interrupt_profile_irq_exit(4, start); // Out-of-range IDs cannot corrupt counters.
 assert(!logs); t5ai_interrupt_profile_dump();
 assert(logs == 7 && strstr(last_log,"reload=119999"));
 assert(real_ticks == 5 && !depth);
 return 0;
}
"""

class InterruptProfileTest(unittest.TestCase):
    def test_actual_counter_pipeline(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            headers = {
                "cmsis_core.h": "#include <stdint.h>\nstatic struct { uint32_t LOAD; } systick;\n#define SysTick (&systick)\n",
                "pbl/kernel/irq.h": "void pbl_irq_lock(void);\nvoid pbl_irq_unlock(void);\n",
                "pbl/logging/logging.h": "#define PBL_LOG_MODULE_DEFINE(...)\nvoid probe_log(const char *, ...);\n#define PBL_LOG_DBG(...) probe_log(__VA_ARGS__)\n",
            }
            for name, contents in headers.items():
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(contents)
            (d / "test.c").write_text(HARNESS)
            exe = d / "test"
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-DCONFIG_T5AI_INTERRUPT_PROFILE=1", "-I", str(d),
                            "-I", str(ROOT / "include"),
                            f'-DPROFILE_SOURCE="{ROOT / "soc/t5ai/interrupt_profile.c"}"',
                            str(d / "test.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_disabled_header_needs_no_runtime(self):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            (d / "test.c").write_text("#include <pbl/soc/t5ai/interrupt_profile.h>\nint main(void) { t5ai_interrupt_profile_start(); t5ai_interrupt_profile_dump(); t5ai_interrupt_profile_irq_exit(0, t5ai_interrupt_profile_irq_enter()); }\n")
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-I",
                            str(ROOT / "include"), str(d / "test.c"),
                            "-o", str(d / "test")], check=True)
            subprocess.run([str(d / "test")], check=True)
