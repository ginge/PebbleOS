# SPDX-License-Identifier: Apache-2.0
"""Verify the active CPU vote and independently observed shared clocks."""

import unittest
from pathlib import Path

from tools.tests.native_t5ai_harness import run

PREFIX = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define CONFIG_KERNEL_TICK_HZ 1000u
typedef unsigned T5AICpuFrequency;
#define T5AICpuFrequency_240MHz 4
#define S_SUCCESS 0
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_DBG(...) ((void)0)
#define PBL_LOG_ERR(...) ((void)0)
#define PROFILER_INIT ((void)0)
#define PROFILER_START ((void)0)
#define SysTick_CTRL_ENABLE_Msk 1u
typedef int status_t;
static struct {uint32_t CTRL,LOAD,VAL;} systick;
#define SysTick (&systick)
static uint32_t event_hz;
static const void *pbl_system_clockevent;
static void pbl_clockevent_set_hz(const void *ce,uint32_t hz){event_hz=hz;}
static uint32_t hw_mode, hw_cpu0, hw_cpu1, hw_ana9, SystemCoreClock;
static unsigned votes;static bool reject;
void t5ai_power_sync_tick_clock(void);
static int t5ai_cp_mailbox_vote_cpu1_frequency(unsigned f){
 assert(f==T5AICpuFrequency_240MHz);
 ++votes;return reject?-1:0;
}
"""
MAIN = r"""
int main(void){
 hw_mode=0x31;hw_cpu1=16;SysTick->CTRL=1;SysTick->LOAD=119999;
 reject=true;t5ai_power_init();assert(!s_initialized && votes==1);
 reject=false;t5ai_power_init();assert(s_initialized && votes==2);
 assert(t5ai_power_clock_hz()==240000000 && SystemCoreClock==240000000 && event_hz==240000000);
 t5ai_power_init();assert(votes==2); // No vote per idle entry or repeated init.
 hw_mode=0x33;t5ai_power_sync_tick_clock();assert(t5ai_power_clock_hz()==120000000 && event_hz==120000000);
 hw_mode=0x10;assert(!t5ai_power_clock_hz()); // Uncalibrated DCO is not guessed.
 return 0;
}
"""


def model(source):
    return (
        source.replace(
            "*(volatile uint32_t *)T5AI_SYSTEM_CLOCK_MODE_ADDRESS", "hw_mode"
        )
        .replace("*(volatile uint32_t *)T5AI_SYSTEM_CPU1_CLOCK_ADDRESS", "hw_cpu1")
        .replace("*(volatile uint32_t *)T5AI_SYSTEM_CPU0_CLOCK_ADDRESS", "hw_cpu0")
        .replace(
            "*(volatile uint32_t *)T5AI_SYSTEM_ANALOG_CONTROL_9_ADDRESS", "hw_ana9"
        )
    )


class PowerVoteTest(unittest.TestCase):
    def test_active_vote_failure_retry_and_actual_elected_clock(self):
        run("soc/t5ai/power.c", PREFIX, MAIN, transform=model)

    def test_snapshot_distinguishes_shared_and_ap_only_dividers(self):
        prefix = (
            PREFIX
            + "\n#define CONFIG_T5AI_POWER_PROFILE 1\n"
            + (
                f'#include "{Path(__file__).resolve().parents[2] / "include/pbl/soc/t5ai/power.h"}"\n'
            )
        )
        main = r"""
int main(void) {
 T5AIClockProfile p;
 hw_mode=0x31;hw_cpu0=hw_cpu1=16;hw_ana9=0xd0000;
 t5ai_power_clock_profile(&p);
 assert(p.cpu0_hz==240000000 && p.ap_hz==240000000 && p.ana9==hw_ana9);
 hw_mode=0x33;hw_ana9=0xc0000;t5ai_power_clock_profile(&p);
 assert(p.cpu0_hz==120000000 && p.ap_hz==120000000);
 hw_mode=0x31;hw_cpu1=0;t5ai_power_clock_profile(&p);
 assert(p.cpu0_hz==240000000 && p.ap_hz==120000000);
 hw_mode=0x10;t5ai_power_clock_profile(&p);assert(!p.cpu0_hz && !p.ap_hz);
 return 0;
}
"""
        run("soc/t5ai/power.c", prefix, main, transform=model)
