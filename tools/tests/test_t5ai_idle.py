# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual idle path with scheduler changes and elapsed ticks."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class IdleTest(unittest.TestCase):
    def test_gate_scheduler_and_accounting(self):
        source = (ROOT/'soc/t5ai/idle.c').read_text()
        function = source[source.index('void pbl_soc_idle'):source.index('bool pbl_soc_tick_enable')]
        common = (ROOT/'fw/kernel/util/idle.c').read_text()
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t RtcTicks;
typedef unsigned pbl_tick_t;
static bool confirm, masked;
static unsigned wfi, confirms, reads, profiles;
static RtcTicks ticks, s_wfi_ticks;

static RtcTicks rtc_get_ticks(void){++reads;return ticks;}
static uint32_t t5ai_interrupt_profile_irq_enter(void){++profiles;return 42;}
static void __disable_irq(void){assert(!masked);masked=true;}
static void __enable_irq(void){assert(masked);masked=false;}
static bool pbl_idle_confirm(void){assert(masked);++confirms;return confirm;}
static void __DSB(void){assert(masked);}
static void __WFI(void){assert(masked && idle_is_allowed() && confirm);++wfi;ticks+=17;}
static void __ISB(void){assert(masked);}
static void t5ai_interrupt_profile_irq_exit(unsigned id,uint32_t start){assert(masked && id==3 && start==42);}
'''
        main = r'''
int main(void){

 idle_set_enabled(false);confirm=true;pbl_soc_idle(100);
 assert(!wfi && !confirms && !reads && !profiles && !s_wfi_ticks && !masked);
 idle_set_enabled(true);confirm=false;pbl_soc_idle(100);
 assert(!wfi && confirms==1 && !s_wfi_ticks && !masked);
 confirm=true;pbl_soc_idle(100);
 assert(wfi==1 && confirms==2 && s_wfi_ticks==17 && !masked);
 idle_set_enabled(false);pbl_soc_idle(0);assert(wfi==1 && s_wfi_ticks==17);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'test.c').write_text('#include <stdbool.h>\n'+common+harness+function+main)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
