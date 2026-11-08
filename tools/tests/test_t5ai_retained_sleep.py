# SPDX-License-Identifier: Apache-2.0
"""Exercise the production retained coordinator against modeled CP and wake endpoints."""

import re
import unittest

from tools.tests.native_t5ai_harness import ROOT, run
from tools.tests.test_t5ai_native_objects import body

RESET_DIAGNOSTICS = (ROOT / "include/pbl/soc/t5ai/reset.h").read_text()
RESET_DIAGNOSTICS = RESET_DIAGNOSTICS[
    RESET_DIAGNOSTICS.index(
        "#define T5AI_RESET_REASON_SLEEP_WAIT_BASE"
    ) : RESET_DIAGNOSTICS.index("#define T5AI_RESET_REASON_MAILBOX_BASE")
]

PREFIX = (
    r"""
static bool t5ai_mailbox_channel_test_timeout_output(void){return true;}
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
_Noreturn void exit(int);
#define CONFIG_T5AI_RETAINED_SLEEP_TEST 1
#define CONFIG_T5AI_RETAINED_SLEEP_TEST_AT_UTC 1000
typedef int32_t time_t;
static time_t wall=1000;
static time_t rtc_get_time(void){return wall;}
#define PBL_LOG_MODULE_DEFINE(...)
static bool in_idle;
static unsigned cleanup_finishes;
#define PBL_LOG_ERR(...) assert(!in_idle)
#define PBL_LOG_DBG(fmt,...) do { \
 assert(!in_idle); \
 if(strstr(fmt,"T5SLEEP votes") || strstr(fmt,"T5SLEEP check entries") || \
    strstr(fmt,"T5SLEEP HAL reject") || strstr(fmt,"T5SLEEP validated")) \
   assert(cleanup_finishes); \
} while(0)
#define T5AI_AON_RTC_HZ 32000u
#define T5AI_SHARED_ALIGNMENT 32u
static uint32_t aon, coordination, mode=0x31, cpu=16;
"""
    + RESET_DIAGNOSTICS
    + r"""
static uint8_t boot_reason;
static uint8_t t5ai_reset_reason_get_boot_ap(void){return boot_reason;}
static uint8_t t5ai_reset_reason_get_boot_cp(void){return 0;}
#define T5AI_AON_RTC_COUNT_LOW aon
#define SCB_SCR_SLEEPDEEP_Msk 4u
static struct {uint32_t SCR,DCCMVAC;} scb;
#define SCB (&scb)
#define __DSB() ((void)0)
typedef int status_t;
#define S_SUCCESS 0
static unsigned locks, diagnostic_queries;
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){assert(locks);--locks;}
static void dcache_flush(void *p,unsigned n){assert(n==64);}
static void dcache_invalidate(void *p,unsigned n){assert(n==64);}
static void (*queued)(void *);
static bool drop, contract_bad, arm_bad, cancel_bad, tick_bad, wake_pending;
static uint32_t query_delay;
static bool cp_contract_ready=true;
static bool guard_bad;
static bool t5ai_watchdog_sleep_guard_arm(void){assert(locks);return !guard_bad;}
static void t5ai_watchdog_sleep_guard_restore(void){assert(locks);}
static bool physical_ready=true, logical_ready=true, hci_ready=true, resume_good=true;
static unsigned arms,cancels,tick_suspends,tick_resumes,ram_entries;
static uint32_t alarm_period, generation, cp_count, cp_resources, cp_clocks, cp_mask_barriers, elapsed=32, arm_delay;
static bool resume_event=true, cp_skip;
static unsigned wake_kicks;
static bool t5ai_native_alarm_event_received(uint32_t gen){assert(gen==generation);return resume_event;}
static void t5ai_mailbox_channel_dump_previous_timeout(void){}
static void t5ai_mailbox_channel_wake(void){assert(!locks);++wake_kicks;}
static void t5ai_force_whole_chip_reset(uint8_t reason){
 assert((CASE==44 || CASE==45) && reason==T5AI_RESET_REASON_SLEEP_WAIT_BASE && !cancels && !locks);
 exit(0);
}

static bool system_task_add_callback_droppable(void (*fn)(void *),void *arg){
 assert(!locks && !arg);if(drop)return false;assert(!queued);queued=fn;return true;
}
static status_t t5ai_native_alarm_init(void){return contract_bad?-1:0;}
static status_t t5ai_native_alarm_cp_query(T5AIAlarmOperation op,uint32_t *out){
 aon+=query_delay;
 if(op==T5AIAlarm_PreviousStage || op==T5AIAlarm_PreviousGeneration)++diagnostic_queries;
 *out=op==T5AIAlarm_MaskBarrierCount ? cp_mask_barriers : op==T5AIAlarm_State ? (cp_contract_ready ? T5AI_SLEEP_REQUIRED_STATE : 0) : op==T5AIAlarm_LowVoltageCount ? cp_count : op==T5AIAlarm_ResourceCount ? cp_resources : op==T5AIAlarm_ClockCount ? cp_clocks : op==T5AIAlarm_ClockModeLow ? (CASE==38?0x31:0) : op==T5AIAlarm_MaskAfter ? ((CASE==42 || CASE==49)?1:0) : op==T5AIAlarm_HighFrequencyLow ? ((CASE==39 || CASE==40)?32:2) : 0;
 return 0;
}
static status_t t5ai_native_alarm_prepare_sleep(uint32_t ticks,uint32_t *gen){
 assert(!locks);++arms;alarm_period=ticks;*gen=++generation;coordination=8;aon+=arm_delay;return arm_bad?-1:0;
}
static status_t t5ai_native_alarm_cancel(uint32_t gen){
 assert(!locks && gen==generation);++cancels;coordination=0;return cancel_bad?-1:0;
}
static status_t t5ai_native_alarm_finish_cleanup(uint32_t gen){assert(!locks && gen==generation && cancels);++cleanup_finishes;return 0;}
static bool t5ai_native_alarm_woken(uint32_t gen){return wake_pending;}
static bool t5ai_mailbox_channel_sleep_ready(void){return physical_ready;}
static bool t5ai_mb_ipc_sleep_ready(void){return logical_ready;}
static bool t5ai_hci_sleep_ready(void){return hci_ready;}
static bool t5ai_tick_prepare_retained(void){assert(locks);if(tick_bad)return false;++tick_suspends;return true;}
static void t5ai_tick_resume_idle(void){assert(locks && tick_suspends>tick_resumes);++tick_resumes;}
static bool modeled_retained_wfi(void){assert(locks);++ram_entries;if(!cp_skip && CASE!=31 && CASE!=32 && CASE!=33 && (CASE<35 || CASE==37 || CASE==50))++cp_count;if(CASE>=35 && CASE!=36)++cp_clocks;if(CASE>=48 && (CASE!=51 || ram_entries>1))++cp_mask_barriers;if(CASE==32 || CASE==34)++cp_resources;aon+=elapsed;coordination=0;return resume_good;}
"""
)
HEADER = (
    (ROOT / "include/pbl/soc/t5ai/sleep_protocol.h").read_text()
    + "\n"
    + (ROOT / "include/pbl/soc/t5ai/sleep.h").read_text()
)
HEADER = re.sub(r"^#(?:include|pragma).*$", "", HEADER, flags=re.MULTILINE)
BLOCKERS = re.sub(
    r"^#include.*$",
    "",
    (ROOT / "soc/t5ai/sleep_blockers.c").read_text(),
    flags=re.MULTILINE,
)


# Hardware WFI/clock restoration is modeled; every production admission/cleanup decision runs unchanged.
def transform(text):
    text = re.sub(
        r"(#define SLEEP_START_DELAY_MS)\s+30000u",
        r"\1 (CASE == 18 ? 30000u : 0u)",
        text,
    )
    text = re.sub(
        r"#define SLEEP_MMIO\(address\).*",
        "#define SLEEP_MMIO(address) ((address)==SLEEP_AON_COORDINATION_ADDRESS?coordination:(address)==SLEEP_CORE_CLOCK_MODE_ADDRESS?mode:cpu)",
        text,
    )
    start = text.index("PBL_SECTION_RAM static bool prv_retained_wfi(")
    end = text.index("static void prv_canary_prepare", start)
    return text[:start] + "#define prv_retained_wfi modeled_retained_wfi\n" + text[end:]


def rtc_nvic_endpoints(text):
    # Model NVIC write-one set/clear aliases without changing admission decisions.
    return (
        text.replace("NVIC->ISER[1] = SLEEP_AP_RTC_INTERRUPT;", "NVIC_EnableIRQ(54);")
        .replace("NVIC->ICER[1] = SLEEP_AP_RTC_INTERRUPT;", "NVIC_DisableIRQ(54);")
        .replace("NVIC->ICPR[1] = SLEEP_AP_RTC_INTERRUPT;", "NVIC_ClearPendingIRQ(54);")
    )


MAIN = r"""
#ifndef EXPECTED_RETURNS
#define EXPECTED_RETURNS 12u
#endif
static void idle_service(uint32_t deadline){in_idle=true;t5ai_sleep_service(deadline);in_idle=false;}
static void worker(void){assert(queued && !locks);void (*fn)(void *)=queued;queued=NULL;fn(NULL);}
static bool enter(uint32_t deadline){pbl_irq_lock();const bool value=t5ai_sleep_enter(deadline);pbl_irq_unlock();return value;}
static void prepare(uint32_t deadline){idle_service(deadline);assert(queued);worker();assert(s_phase==SleepArmed);idle_service(deadline);assert(s_phase==SleepPrepared);}
int main(void){
 idle_service(3200);assert(!queued);
#if CASE == 15
 #ifndef BOOT_TIMEOUT_REASON
#define BOOT_TIMEOUT_REASON T5AI_RESET_REASON_SLEEP_RESUME
#endif
 boot_reason=BOOT_TIMEOUT_REASON;
#endif
#if CASE >= 23 && CASE <= 25
 s_sleep_trace.magic=T5AI_SLEEP_TRACE_MAGIC;
 s_sleep_trace.stage=CASE==23 ? T5AISleepStage_AP_Published : CASE==24 ? T5AISleepStage_AP_ClocksRestored : SLEEP_TRACE_COMPLETE;
 s_sleep_trace.inverse=~s_sleep_trace.stage;
#endif
#if CASE == 14
 cp_count=7;
#endif
 t5ai_sleep_start();
#if CASE != 15 && CASE != 23 && CASE != 24
 assert(queued);worker();assert(diagnostic_queries==2 && !arms && !ram_entries);
#endif
#if CASE == 47
 // Even lost AP reset evidence must not hide a surviving CP breadcrumb.
 assert(!boot_reason && !s_sleep_trace.magic && s_phase==SleepIdle && !s_stats.fault);
 assert(diagnostic_queries==2 && !queued && !arms && !ram_entries);
#elif CASE == 0
 prepare(3200);assert(alarm_period==2688 && arms==1);
 // Balanced ownership after preparation invalidates the epoch, even if idle again.
 t5ai_sleep_block(T5AISleepOwner_SD);t5ai_sleep_unblock(T5AISleepOwner_SD);
 assert(!enter(3200) && !ram_entries && !tick_suspends);
 idle_service(3200);worker();assert(cancels==1 && s_phase==SleepIdle);
 prepare(3200);assert(enter(3200) && ram_entries==1 && tick_resumes==1);
 idle_service(3200);worker();assert(cancels==2 && s_stats.cp_entries==1);
#elif CASE >= 43 && CASE <= 46
 if(CASE==45)aon=UINT32_MAX-100;
 prepare(aon+3200);resume_event=false;assert(enter(aon+3200));
 idle_service(aon+3200);
 assert(s_phase==SleepAwaitController && !queued && !cancels && wake_kicks==1);
 if(CASE==44 || CASE==45){
  aon+=SLEEP_CP_EVENT_TIMEOUT_MS*SLEEP_AON_PER_MS-1;
  idle_service(aon+3200);assert(!queued && !cancels && wake_kicks==1);
  ++aon;idle_service(aon+3200);assert(false);
 }
 if(CASE==46)wall+=SLEEP_TEST_SLOT_SECONDS;
 resume_event=true;idle_service(aon+3200);worker();
 assert(cancels==1 && tick_resumes==1 && !s_stats.fault && wake_kicks==1);
#elif CASE == 1
 drop=true;idle_service(3200);assert(!queued && s_phase==SleepIdle && s_stats.dropped==1);
 drop=false;prepare(3200);aon=3000;assert(!enter(3200));
 drop=true;idle_service(3200);assert(s_phase==SleepNeedsCleanup && !queued);
 drop=false;idle_service(3200);worker();assert(s_phase==SleepIdle && cancels==1);
#elif CASE == 2
 prepare(3200);physical_ready=false;assert(!enter(3200));physical_ready=true;
 logical_ready=false;assert(!enter(3200));logical_ready=true;
 hci_ready=false;assert(!enter(3200));hci_ready=true;
 wake_pending=true;assert(!enter(3200));wake_pending=false;
 coordination=0;assert(!enter(3200));coordination=8;
 mode=0x33;assert(!enter(3200));mode=0x31;
 assert(!enter(2900));tick_bad=true;assert(!enter(3200));tick_bad=false;
 assert(!ram_entries && !tick_resumes);assert(enter(3200));
#elif CASE == 3
 aon=UINT32_MAX-100;prepare(aon+3200);assert(enter(aon+3200));
 idle_service(aon+3200);worker();assert(!s_stats.fault && s_stats.entries==1);
#elif CASE == 4
 s_initialized=false;contract_bad=true;idle_service(3200);worker();assert(s_phase==SleepFailed && !arms);
 assert(t5ai_sleep_blockers().faulted && !enter(3200));
#elif CASE == 5
 arm_bad=true;idle_service(3200);worker();assert(s_phase==SleepFailed && arms==1);
 assert(!enter(3200) && t5ai_sleep_blockers().faulted);
#elif CASE == 6
 prepare(3200);resume_good=false;assert(!enter(3200) && tick_resumes==1);
 assert(!s_stats.fault && s_stats.aborted==1 && !s_stats.entries);
 idle_service(3200);worker();assert(s_phase==SleepIdle && cancels==1);
#elif CASE == 7
 prepare(3200);assert(enter(3200));s_psram_canary[4]^=1;
 idle_service(3200);worker();assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_Cleanup);
#elif CASE == 8
 prepare(3200);elapsed=3201;assert(enter(3200) && tick_resumes==1);
 assert(s_stats.fault==T5AISleepFault_Deadline && s_stats.late_aon==1);
#elif CASE == 9
 // Exact-once tick resume and stale worker rejection across all bounded entries.
 for(unsigned i=0;i<SLEEP_TEST_ENTRY_LIMIT;++i){
  prepare(aon+3200);assert(enter(aon+3200));assert(!enter(aon+3200));
  idle_service(aon+3200);worker();
 }
 assert(s_phase==SleepTestComplete && ram_entries==EXPECTED_RETURNS && cancels==EXPECTED_RETURNS && tick_resumes==EXPECTED_RETURNS);
 idle_service(aon+3200);assert(!queued);
#elif CASE == 51
 prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 assert(s_phase==SleepIdle && !s_stats.cp_mask_barriers && s_stats.cp_clock_cycles==1);
 prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 assert(s_phase==SleepTestComplete && s_stats.cp_mask_barriers==1 && s_stats.cp_clock_cycles==2 && !s_stats.cp_entries);
#elif CASE >= 48 && CASE <= 50
 prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 if(CASE==48)assert(s_phase==SleepTestComplete && !s_stats.cp_entries && s_stats.cp_clock_cycles==1 && !s_stats.fault);
 else assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_Contract);
#elif CASE >= 35 && CASE <= 42
 for(unsigned i=0;i<SLEEP_TEST_ENTRY_LIMIT;++i){
  prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 }
 if(CASE==35 || CASE==40)assert(s_phase==SleepTestComplete && s_stats.cp_clock_cycles==12 && !s_stats.cp_entries && !s_stats.fault);
 else assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_Contract);
#elif CASE == 32 || CASE == 33 || CASE == 34
 for(unsigned i=0;i<SLEEP_TEST_ENTRY_LIMIT;++i){
  prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 }
 if(CASE==32)assert(s_phase==SleepTestComplete && s_stats.cp_resources==12 && !s_stats.cp_entries && !s_stats.fault);
 else assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_Contract);
#elif CASE == 31
 for(unsigned i=0;i<SLEEP_TEST_ENTRY_LIMIT;++i){
  prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 }
 assert(s_phase==SleepTestComplete && !s_stats.cp_entries && !s_stats.fault);
#elif CASE == 10
 t5ai_sleep_block(T5AISleepOwner_I2C);t5ai_sleep_block(T5AISleepOwner_I2C);
 assert(t5ai_sleep_blockers().owners==(1u<<T5AISleepOwner_I2C));
 t5ai_sleep_unblock(T5AISleepOwner_I2C);assert(t5ai_sleep_blockers().owners);
 t5ai_sleep_unblock(T5AISleepOwner_I2C);assert(!t5ai_sleep_blockers().owners);
 t5ai_sleep_unblock(T5AISleepOwner_I2C);assert(t5ai_sleep_blockers().faulted);
#elif CASE == 11
 s_blockers.epoch=UINT32_MAX;t5ai_sleep_block(T5AISleepOwner_Flash);
 assert(t5ai_sleep_blockers().faulted && !t5ai_sleep_blockers_match(UINT32_MAX));
#elif CASE == 12
 t5ai_sleep_block((T5AISleepOwner)-1);assert(t5ai_sleep_blockers().faulted);
#elif CASE == 19
 // Applied-response latency consumes its own budget, not the wake margin.
 arm_delay=64;prepare(3200);assert(alarm_period==2688 && enter(3200));
 assert(tick_resumes==1 && !s_stats.fault);
#elif CASE == 20
 prepare(3200);assert(!enter(2900));
 idle_service(2900);worker();assert(cancels==1 && s_phase==SleepIdle && !ram_entries);
#elif CASE == 21
 // An over-budget arm is cancelled immediately; the deadline guard stays strict.
 arm_delay=320;idle_service(3200);worker();assert(s_phase==SleepArmed);
 assert(!enter(3200));idle_service(3200);assert(s_phase==SleepCleanupQueued);
 worker();assert(cancels==1 && s_phase==SleepIdle && !ram_entries && !tick_suspends);
#elif CASE == 18
 // Startup work settles before the bounded attempt budget begins.
 idle_service(3200);assert(!queued && !arms);
 aon=s_test_not_before-1;idle_service(aon+3200);assert(!queued);
 ++aon;prepare(aon+3200);assert(arms==1 && enter(aon+3200));
#elif CASE == 16
 // Complete prepare-response ACK/RX activity before taking the final epoch.
 idle_service(3200);worker();assert(s_phase==SleepArmed);
 physical_ready=false;t5ai_sleep_block(T5AISleepOwner_Mailbox);
 idle_service(3200);assert(s_phase==SleepArmed && !enter(3200));
 t5ai_sleep_unblock(T5AISleepOwner_Mailbox);physical_ready=true;
 idle_service(3200);assert(s_phase==SleepPrepared && enter(3200));
#elif CASE == 17
 // An aborted pilot is bounded even when no WFI entry ever succeeds.
 for(unsigned i=0;i<SLEEP_TEST_REQUEST_LIMIT;++i){
  prepare(aon+3200);t5ai_sleep_block(T5AISleepOwner_SD);t5ai_sleep_unblock(T5AISleepOwner_SD);
  assert(!enter(aon+3200));idle_service(aon+3200);worker();
 }
 assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_AdmissionBudget);
 assert(!ram_entries && cancels==SLEEP_TEST_REQUEST_LIMIT);
 idle_service(aon+3200);assert(!queued);
#elif CASE == 15
 assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_ResumeTimeout);
 assert(queued);worker();assert(!arms && !ram_entries); // Read-only previous-stage recovery.
 idle_service(3200);assert(!queued && !enter(3200));
#elif CASE == 14
 // A previous CP residency count cannot qualify this AP session.
 cp_count=7;prepare(3200);assert(s_cp_entries_start==7);
 assert(enter(3200));idle_service(3200);worker();
 assert(s_stats.cp_entries==1);
#elif CASE == 23 || CASE == 24
 assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_ResumeTimeout);
 assert(queued);worker();assert(!arms && !ram_entries);
 idle_service(3200);assert(!queued && !enter(3200));
#elif CASE == 25
 assert(s_phase==SleepIdle);prepare(3200);assert(enter(3200));idle_service(3200);worker();
 assert(s_sleep_trace.stage==SLEEP_TRACE_COMPLETE && s_sleep_trace.inverse==~SLEEP_TRACE_COMPLETE);
#elif CASE == 22
 guard_bad=true;prepare(3200);assert(!enter(3200));
 assert(s_stats.fault==T5AISleepFault_WatchdogGuard && !ram_entries);
 assert(tick_suspends==1 && tick_resumes==1 && t5ai_sleep_blockers().faulted);
#elif CASE == 26
 wall=999;idle_service(3200);assert(!queued && !arms);
 wall=1000+SLEEP_TEST_SLOT_SECONDS;idle_service(3200);assert(queued && !arms);
 worker();assert(s_test_reported && !s_test_report_queued && !arms);
 idle_service(3200);assert(!queued);
#elif CASE == 28
 idle_service(3200);assert(queued);wall=1000+SLEEP_TEST_SLOT_SECONDS;worker();
 assert(!arms && s_phase==SleepIdle);
#elif CASE == 27
 prepare(3200);wall=1000+SLEEP_TEST_SLOT_SECONDS;assert(!enter(3200));
 idle_service(3200);worker();assert(cancels==1 && !ram_entries);
#elif CASE == 29
 prepare(3200);assert(enter(3200));wall=1000+SLEEP_TEST_SLOT_SECONDS;
 idle_service(3200);worker();assert(cancels==1 && !s_test_reported);
 idle_service(3200);worker();assert(s_test_reported && arms==1);
 idle_service(3200);assert(!queued);
#elif CASE == 30
 wall=1000+SLEEP_TEST_SLOT_SECONDS;drop=true;idle_service(3200);
 assert(!queued && !s_test_report_queued && !s_test_reported);
 drop=false;idle_service(3200);worker();assert(s_test_reported && !arms);
#elif CASE == 13
 s_owners[T5AISleepOwner_DMA]=UINT16_MAX;t5ai_sleep_block(T5AISleepOwner_DMA);
 assert(t5ai_sleep_blockers().faulted && s_owners[T5AISleepOwner_DMA]==UINT16_MAX);
#endif
 assert(!locks);return 0;
}
"""


class RetainedSleep(unittest.TestCase):
    def test_startup_preflight_defers_missing_peripheral_votes(self):
        main = (
            MAIN[: MAIN.index("int main(void)")]
            + r"""
int main(void){
 cp_contract_ready=false;t5ai_sleep_start();worker();
 assert(!s_initialized && !s_stats.fault && s_phase==SleepIdle && !arms);
 cp_contract_ready=true;prepare(aon+3200);
 assert(s_initialized && arms==1 && !s_stats.fault && !locks);return 0;
}
"""
        )
        run(
            "soc/t5ai/sleep.c",
            "#include <stdbool.h>\n#include <stdint.h>\n" + HEADER + PREFIX + BLOCKERS,
            main,
            defines=("CASE=2",),
            transform=transform,
        )

    def test_awake_preflight_preserves_the_first_idle_deadline(self):
        main = (
            MAIN[: MAIN.index("int main(void)")]
            + r"""
int main(void){
 query_delay=160; t5ai_sleep_start();worker();
 assert(s_initialized && !arms && !ram_entries && aon>320);
 query_delay=0;
 const uint32_t deadline=aon+SLEEP_MIN_WINDOW_MS*SLEEP_AON_PER_MS;
 prepare(deadline);
 assert(arms==1 && s_target==deadline && !s_stats.fault);
 assert(alarm_period>=T5AI_SLEEP_ANALOG_WAKE_MIN_MS*SLEEP_AON_PER_MS);
 assert(!locks);return 0;
}
"""
        )
        run(
            "soc/t5ai/sleep.c",
            "#include <stdbool.h>\n#include <stdint.h>\n" + HEADER + PREFIX + BLOCKERS,
            main,
            defines=("CASE=2", "CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST=1"),
            transform=transform,
        )

    def test_analog_refusals_stop_after_first_cp_return(self):
        main = (
            MAIN[: MAIN.index("int main(void)")]
            + r"""
int main(void){
 t5ai_sleep_start();worker();cp_skip=true;
 for(unsigned attempt=0;attempt<3;++attempt){
  if(attempt==2)cp_skip=false;
  const uint32_t deadline=aon+3200;prepare(deadline);assert(enter(deadline));
  idle_service(deadline);worker();assert(!s_stats.fault && s_stats.entries==attempt+1);
  assert(s_phase==(attempt==2 ? SleepTestComplete : SleepIdle));
 }
 assert(s_stats.cp_entries==1 && s_stats.entries==3);
 idle_service(aon+3200);assert(!queued && !locks && arms==3);return 0;
}
"""
        )
        run(
            "soc/t5ai/sleep.c",
            "#include <stdbool.h>\n#include <stdint.h>\n" + HEADER + PREFIX + BLOCKERS,
            main,
            defines=(
                "CASE=2",
                "CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST=1",
                "CONFIG_T5AI_RETAINED_SLEEP_TEST_ENTRY_LIMIT=8",
            ),
            transform=transform,
        )

    def test_bounded_analog_budget_preserves_caller_deadline(self):
        main = (
            MAIN[: MAIN.index("int main(void)")]
            + r"""
int main(void){
 t5ai_sleep_start();worker();
 assert(SLEEP_MIN_WINDOW_MS==44 && SLEEP_WAKE_MARGIN_MS==8);
 if(ANALOG_CASE==3)aon=UINT32_MAX-100;
 const uint32_t deadline=aon+SLEEP_MIN_WINDOW_MS*SLEEP_AON_PER_MS;
 idle_service(deadline-1);assert(!queued && !arms);
 if(ANALOG_CASE>=4){
  idle_service(deadline);assert(queued);aon+=ANALOG_CASE==4 ? 64u : 65u;worker();
  if(ANALOG_CASE==4){
   assert(arms==1 && s_phase==SleepArmed);
   assert(alarm_period==T5AI_SLEEP_ANALOG_WAKE_MIN_MS*SLEEP_AON_PER_MS);
   s_phase=SleepNeedsCleanup;idle_service(deadline);worker();assert(cancels==1);
  }else assert(!arms && s_phase==SleepIdle && s_worker_budget_refusals==1);
  assert(!locks);return 0;
 }
 arm_delay=(ANALOG_CASE==1 ? 33u : 32u);
 idle_service(deadline);assert(queued);worker();assert(s_phase==SleepArmed);
 assert(alarm_period==(T5AI_SLEEP_ANALOG_WAKE_MIN_MS+SLEEP_ANALOG_PUBLICATION_RESERVE_MS)*SLEEP_AON_PER_MS);
 idle_service(deadline);
 if(ANALOG_CASE==1){
  assert(s_phase==SleepCleanupQueued && !ram_entries);worker();
  assert(cancels==1 && !s_stats.fault);return 0;
 }
 assert(s_phase==SleepPrepared);
 if(ANALOG_CASE==2){
  assert(!enter(deadline-1) && !ram_entries && !tick_suspends);return 0;
 }
 elapsed=T5AI_SLEEP_ANALOG_WAKE_PERIOD_MS*SLEEP_AON_PER_MS;
 assert(enter(deadline) && !s_stats.late_aon && !s_stats.fault);
 assert((int32_t)(deadline-aon)>=(int32_t)(SLEEP_WAKE_MARGIN_MS*SLEEP_AON_PER_MS));
 idle_service(deadline);worker();assert(cancels==1 && !locks);return 0;
}
"""
        )
        for case in range(6):
            run(
                "soc/t5ai/sleep.c",
                "#include <stdbool.h>\n#include <stdint.h>\n"
                + HEADER
                + PREFIX
                + BLOCKERS,
                main,
                defines=(
                    "CASE=2",
                    "CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST=1",
                    f"ANALOG_CASE={case}",
                ),
                transform=transform,
            )

    def test_deadline_profile_observes_without_arming(self):
        profile = r"""
/* SPDX-License-Identifier: Apache-2.0 */


#define T5AI_SLEEP_PROFILE_CANDIDATES 8u
typedef struct {
  uint32_t id, remaining_ms, period_ms;
  uintptr_t callback, context;
} T5AISleepTimerCandidate;
static unsigned t5ai_sleep_profile_timer_candidates(T5AISleepTimerCandidate *out);

static unsigned t5ai_sleep_profile_timer_candidates(T5AISleepTimerCandidate *out){return 0;}
static void t5ai_sleep_profile_timers_ready(void){}
static unsigned t5ai_sleep_profile_timers_untracked(void){return 0;}
#define CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY 1
#define PBL_THREAD_NAME_LEN 16
#define PBL_THREAD_REG_PC 0
typedef uint32_t pbl_tick_t;
struct pbl_thread {struct {bool on_timeout_list;uint32_t wake_at;} backend;};
struct pbl_thread_info {struct pbl_thread *thread;const char *name;uint32_t regs[1];};
static uint64_t pbl_uptime_ticks(void){assert(locks);return 100;}
static pbl_tick_t timer_ticks_until_next(pbl_tick_t now){assert(locks && now==100);return 15;}
static void pbl_thread_foreach(void (*fn)(const struct pbl_thread_info *,void *),void *ctx){
 assert(locks);struct pbl_thread a={.backend={true,130}},b={.backend={true,110}};
 struct pbl_thread_info x={&a,"later",{0x1234}},y={&b,"poller",{0x5678}};
 fn(&x,ctx);fn(&y,ctx);
}
"""
        main = r"""
int main(void){
 t5ai_sleep_start();assert(queued);void (*fn)(void *)=queued;queued=NULL;fn(NULL);
 s_test_not_before=0;t5ai_sleep_service(1600);
 assert(s_budget_calls==1 && s_budget_max_aon==1600 && s_budget_timer_ticks==15);
 assert(s_budget_thread_ticks==10 && !strcmp(s_budget_thread_name,"poller") && s_budget_thread_pc==0x5678);
 assert(!arms && !ram_entries && !queued && s_phase==SleepIdle);
 t5ai_sleep_service(800);assert(s_budget_calls==2 && s_budget_max_aon==1600 && !arms);
 wall=1021;t5ai_sleep_service(800);assert(queued);fn=queued;queued=NULL;fn(NULL);
 assert(s_test_reported && !arms && !ram_entries && !locks);
 return 0;
}
"""
        run(
            "soc/t5ai/sleep.c",
            "#include <stdint.h>\n#include <stdbool.h>\n"
            + HEADER
            + PREFIX
            + BLOCKERS
            + profile,
            main,
            defines=["CASE=2"],
            transform=transform,
        )

    def test_physical_ack_and_logical_buffer_leases(self):
        physical = body(
            (ROOT / "drivers/t5ai/mailbox_channel.c").read_text(),
            "t5ai_mailbox_channel_sleep_ready",
        )
        logical = body(
            (ROOT / "drivers/t5ai/mb_ipc.c").read_text(), "t5ai_mb_ipc_sleep_ready"
        )
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define T5AI_MAILBOX_CHANNEL_COUNT 16
#define MB_IPC_SOCKET_COUNT 4
#define MBOX_FIFO_NOT_EMPTY (1u<<19)
static uint32_t mailbox_control;
#define MBOX_CH1_CTRL mailbox_control
#define TxState_Idle 0
#define AsyncIdle 0
static bool s_started,s_faulted,s_initialized;
static uint32_t s_pending_ack_mask;
static struct {unsigned state;} s_tx[16];
typedef struct {bool allocated,poisoned,tx_in_process,rx_in_process;struct {unsigned phase;} async;} T5AIMbIpcSocket;
static T5AIMbIpcSocket s_sockets[4];
"""
        main = r"""
int main(void){
 assert(!t5ai_mailbox_channel_sleep_ready() && !t5ai_mb_ipc_sleep_ready());
 s_started=s_initialized=true;assert(t5ai_mailbox_channel_sleep_ready() && t5ai_mb_ipc_sleep_ready());
 mailbox_control=MBOX_FIFO_NOT_EMPTY;assert(!t5ai_mailbox_channel_sleep_ready());mailbox_control=0;
 s_faulted=true;assert(!t5ai_mailbox_channel_sleep_ready());s_faulted=false;
 s_pending_ack_mask=1;assert(!t5ai_mailbox_channel_sleep_ready());s_pending_ack_mask=0;
 for(unsigned i=0;i<16;++i){s_tx[i].state=1;assert(!t5ai_mailbox_channel_sleep_ready());s_tx[i].state=0;}
 s_sockets[3].allocated=true;
 s_sockets[3].tx_in_process=true;assert(!t5ai_mb_ipc_sleep_ready());s_sockets[3].tx_in_process=false;
 s_sockets[3].rx_in_process=true;assert(!t5ai_mb_ipc_sleep_ready());s_sockets[3].rx_in_process=false;
 s_sockets[3].async.phase=1;assert(!t5ai_mb_ipc_sleep_ready());s_sockets[3].async.phase=0;
 s_sockets[3].poisoned=true;assert(!t5ai_mb_ipc_sleep_ready());
 assert(t5ai_mailbox_channel_sleep_ready()); // Physical ACK alone cannot release a logical lease.
 return 0;
}
"""
        run("soc/t5ai/sleep.c", prefix, main, transform=lambda _: physical + logical)

    def test_actual_sram_rtc_wake_route_ownership_and_cleanup(self):
        prefix = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define PBL_SECTION_RAM
"""
            + RESET_DIAGNOSTICS
            + r"""
#define T5AI_AON_RTC_HZ 32000u
#define T5AI_AON_RTC_COUNT_LOW aon
#define SCB_SCR_SLEEPDEEP_Msk 4u
#define SCB_ICSR_VECTPENDING_Msk 0x1ff000u
#define SCB_ICSR_PENDSTSET_Msk (1u<<26)
#define SLEEP_MARK_STAGE(stage) (++stages)
static uint32_t aon, primask, stages, wfis;
static uint32_t prepared_read_checks;
static struct {uint32_t ISER[2],ISPR[2];} nvic;
static struct {uint32_t SCR,ICSR;} scb;
#define NVIC (&nvic)
#define SCB (&scb)
typedef unsigned IRQn_Type;
static uint32_t __get_PRIMASK(void){return primask;}
static void __disable_irq(void){primask=1;}
static void __set_PRIMASK(uint32_t v){primask=v;}
#define __DSB() ((void)0)
#define __ISB() ((void)0)
static uint32_t regs[16];
static uint32_t read_sleep_control(void){
 ++prepared_read_checks;assert(!(regs[4]&2u));
 return CASE==8?scb.SCR&~4u:scb.SCR;
}
static uint32_t *reg(uint32_t address){
 switch(address){
 case 0x44010088u:return &regs[0];case 0x4401008cu:return &regs[1];
 case 0x440100a8u:return &regs[2];case 0x440100acu:return &regs[3];
 case 0x4400000cu:return &regs[4];case 0x44010020u:return &regs[5];
 case 0x44010014u:return &regs[6];case 0x44010010u:return &regs[7];
 case 0x44010024u:return &regs[8];default:assert(false);return 0;
 }
}
static void NVIC_EnableIRQ(unsigned irq){assert(irq==54 && primask);nvic.ISER[1]|=1u<<22;if(CASE==7)nvic.ISPR[1]|=1u<<22;}
static void NVIC_DisableIRQ(unsigned irq){assert(irq==54 && primask);nvic.ISER[1]&=~(1u<<22);}
static void NVIC_ClearPendingIRQ(unsigned irq){assert(irq==54 && primask);nvic.ISPR[1]&=~(1u<<22);}
static void t5ai_force_whole_chip_reset_from_sram(unsigned reason){assert(false);}
static void __WFI(void){
 assert(primask && (scb.SCR&4) && (regs[4]&2));
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_WAKE_ONLY
 assert(!regs[0] && regs[1]==(1u<<31) && (nvic.ISER[1]&(1u<<22)));
#else
 assert(!regs[0] && regs[1]==((1u<<31)|(1u<<22)) && (nvic.ISER[1]&(1u<<22)));
#endif
 ++wfis;aon+=32;nvic.ISPR[1]|=1u<<22;regs[4]&=~8u;
}
"""
        )
        main = r"""
int main(void){
 regs[0]=0x1234;regs[1]=1u<<31;regs[4]=8;regs[5]=0x31;regs[6]=regs[7]=16;regs[8]=0x05000010;
#ifdef CONFIG_T5AI_RETAINED_SLEEP_CP_WAKE_ONLY
 regs[1]|=1u<<23; // Preserve an existing AP GPIO owner through the CP-only route.
#endif
 nvic.ISER[1]=1u<<31;scb.SCR=0x10;primask=CASE==1;
 if(CASE==2)nvic.ISER[1]|=1u<<22;
 if(CASE==3)nvic.ISPR[1]|=1u<<22;
 if(CASE==4)regs[1]|=1u<<22;
 if(CASE==5)regs[2]=1;
 if(CASE==6)nvic.ISER[0]=nvic.ISPR[0]=1;
 const uint32_t initial_enable=nvic.ISER[1],initial_pending=nvic.ISPR[1],initial_route=regs[1];
 const bool result=prv_retained_wfi();
 assert(result==(CASE<2));assert(wfis==(CASE<2));assert(primask==(CASE==1));
 assert(regs[0]==0x1234 && regs[1]==initial_route && scb.SCR==0x10);
 assert(nvic.ISER[1]==initial_enable && nvic.ISPR[1]==initial_pending);
 assert((regs[4]&2)==0);return 0;
}
"""
        source = (ROOT / "soc/t5ai/sleep.c").read_text()
        defines = source[
            source.index("#define SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS") : source.index(
                "PBL_LOG_MODULE_DEFINE"
            )
        ]
        defines = re.sub(
            r"#define SLEEP_MMIO\(address\).*",
            "#define SLEEP_MMIO(address) (*reg(address))",
            defines,
        )
        helper = rtc_nvic_endpoints(body(source, "prv_retained_wfi"))
        helper = helper.replace(
            "const uint32_t prepared_sleep_control = SCB->SCR;",
            "const uint32_t prepared_sleep_control = read_sleep_control();",
        )
        for cp_only in (False, True):
            for case in range(9):
                run(
                    "soc/t5ai/sleep.c",
                    prefix,
                    main,
                    defines=[f"CASE={case}"]
                    + (
                        ["CONFIG_T5AI_RETAINED_SLEEP_CP_WAKE_ONLY=1"] if cp_only else []
                    ),
                    transform=lambda _: defines + helper,
                )

    def test_receive_lease_spans_callbacks_and_failed_snapshots(self):
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define MAILBOX_RUNTIME_DRAIN_LIMIT 4u
#define MBOX_FIFO_NOT_EMPTY 1u
#define MBOX_ACK_BOX 2u
#define T5AISleepOwner_Mailbox 7u
static uint32_t mailbox_control;
#define MBOX_CH1_CTRL mailbox_control
typedef struct {uint32_t header;} T5AIMailboxMessage;
static unsigned held,blocks,unblocks,snapshots,callbacks;
static bool snapshot_fails,ack;
static void t5ai_sleep_block(unsigned owner){assert(owner==7 && !held);++held;++blocks;}
static void t5ai_sleep_unblock(unsigned owner){assert(owner==7 && held==1);--held;++unblocks;}
static bool prv_snapshot_rx(T5AIMailboxMessage *m){
 assert(held==1);++snapshots;
 if(snapshot_fails)return false;
 mailbox_control=0;m->header=ack?MBOX_ACK_BOX:0;return true;
}
static void prv_process_ack(const T5AIMailboxMessage *m){assert(held==1 && ack);++callbacks;}
static void prv_process_command(const T5AIMailboxMessage *m){assert(held==1 && !ack);++callbacks;}
"""
        main = r"""
int main(void){
 assert(!prv_drain_rx() && blocks==1 && unblocks==1 && !snapshots);
 mailbox_control=1;assert(!prv_drain_rx() && callbacks==1 && !held);
 ack=true;mailbox_control=1;assert(!prv_drain_rx() && callbacks==2 && !held);
 snapshot_fails=true;mailbox_control=1;unsigned before=snapshots;
 assert(prv_drain_rx() && snapshots-before==4 && callbacks==2 && !held);
 assert(blocks==4 && unblocks==4);return 0;
}
"""
        run(
            "drivers/t5ai/mailbox_channel.c",
            prefix,
            main,
            defines=("CONFIG_T5AI_RETAINED_SLEEP",),
            transform=lambda source: body(source, "prv_drain_rx"),
        )

    def test_sleep_timeout_reason_namespace(self):
        prefix = "#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n"
        main = """
int main(void){
 for(unsigned reason=0;reason<256;++reason)
  assert(t5ai_reset_reason_is_sleep_timeout(reason)==
         (reason>=0x40 && reason<=0x4f));
 return 0;
}
"""
        run(
            "soc/t5ai/sleep.c",
            prefix + RESET_DIAGNOSTICS,
            main,
            transform=lambda source: "",
        )

    def test_every_restore_timeout_quarantines_next_boot(self):
        for reason in range(0x40, 0x50):
            with self.subTest(reason=reason):
                run(
                    "soc/t5ai/sleep.c",
                    "#include <stdbool.h>\n#include <stdint.h>\n"
                    + HEADER
                    + PREFIX
                    + BLOCKERS,
                    MAIN,
                    defines=("CASE=15", f"BOOT_TIMEOUT_REASON={reason}"),
                    transform=transform,
                )

    def test_actual_sram_restore_gate(self):
        prefix = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#define PBL_SECTION_RAM
#define T5AI_AON_RTC_HZ 32000u
#define T5AI_SHARED_ALIGNMENT 32u
"""
            + RESET_DIAGNOSTICS
            + r"""
static uint32_t aon,hw_coordination=8,hw_mode=0x31,hw_ap=16,hw_cp=16,hw_flash=0x09000000;
#define T5AI_AON_RTC_COUNT_LOW (aon+=32)
static struct {uint32_t SCR,DCCMVAC,ICSR;} scb={.SCR=0x10};
#define SCB_ICSR_VECTPENDING_Msk 0x1ff000u
#define SCB_ICSR_PENDSTSET_Msk (1u<<26)
static uint32_t irq_low=0x123,irq_high=0x80000300,status_low,status_high;
static struct {uint32_t ISPR[2],ISER[2];} nvic={.ISER={0xffffffffu,0xffbfffffu}};
#define NVIC (&nvic)
static struct {uint32_t magic,generation,stage,inverse;} s_sleep_trace;
#define SLEEP_MARK_STAGE(value) do {s_sleep_trace.stage=(value);} while(0)
#define T5AISleepStage_AP_Published 1u
#define T5AISleepStage_AP_Woke 2u
#define T5AISleepStage_AP_ClocksRestored 3u
#define SCB (&scb)
typedef unsigned IRQn_Type;
static void NVIC_EnableIRQ(unsigned irq){assert(irq==54);nvic.ISER[1]|=1u<<22;}
static void NVIC_DisableIRQ(unsigned irq){assert(irq==54);nvic.ISER[1]&=~(1u<<22);}
static void NVIC_ClearPendingIRQ(unsigned irq){assert(irq==54);nvic.ISPR[1]&=~(1u<<22);}
#define SCB_SCR_SLEEPDEEP_Msk 4u
#define __DSB() ((void)0)
#define __ISB() ((void)0)
static unsigned hw_primask;
#define __get_PRIMASK() hw_primask
#define __disable_irq() (hw_primask=1)
#define __set_PRIMASK(value) (hw_primask=(value))
static unsigned wfi_count;
static jmp_buf failed;
static void t5ai_force_whole_chip_reset_from_sram(uint8_t reason){
 const unsigned blockers = (CASE>=15 ? (unsigned)(CASE-15) : CASE==6 ? T5AI_RESET_REASON_SLEEP_WAIT_READY :
   CASE==5 || CASE==14 ? T5AI_RESET_REASON_SLEEP_WAIT_MEMORY : T5AI_RESET_REASON_SLEEP_WAIT_CORE);
 assert(reason==(T5AI_RESET_REASON_SLEEP_WAIT_BASE|blockers) && (hw_coordination&2) && (scb.SCR&4));
 assert(aon>=22400 && aon<=22464);longjmp(failed,1);
}
static void modeled_wake(void){
 assert((hw_coordination&2) && (scb.SCR&4) && hw_primask);
 assert(!irq_low && irq_high==0x80400000);
 hw_coordination&=~8u;
#if CASE == 1
 hw_mode=0x30; // Faster than before still fails exact restoration.
#elif CASE == 2
 hw_mode^=64; // Bus divider mismatch.
#elif CASE == 3
 hw_ap=0;
#elif CASE == 4
 hw_cp=0;
#elif CASE == 5
 hw_flash^=1u<<24;
#elif CASE == 6
 hw_coordination|=8;
#elif CASE == 14
 hw_flash^=1u<<24;
#elif CASE >= 15
 if((CASE-15)&1)hw_coordination|=8;
 if((CASE-15)&2)hw_cp=0;
 if((CASE-15)&4)hw_flash^=1u<<24;
#elif CASE == 7
 hw_mode^=1u<<8;hw_flash^=1u<<14; // Unrelated UART/SD fields do not count.
#endif
}
static void modeled_wfi(void){++wfi_count;modeled_wake();}
#define __WFI() modeled_wfi()
#if CASE == 13 || CASE == 14
#undef T5AI_AON_RTC_COUNT_LOW
static uint32_t advance_aon(void){
 aon+=32;
 if(aon==96)modeled_wake();
 return aon;
}
#define T5AI_AON_RTC_COUNT_LOW advance_aon()
#endif
"""
        )
        main = r"""
int main(void){
#if CASE >= 8 && CASE <= 12
 if(CASE==8)status_low=1;else if(CASE==9)status_high=1;else if(CASE==10)scb.ICSR=0x1000;
 else nvic.ISPR[CASE-11]=1;
 assert(!prv_retained_wfi() && !wfi_count && !(hw_coordination&2));
 assert(irq_low==0x123 && irq_high==0x80000300 && !hw_primask);return 0;
#endif
 if(!setjmp(failed)){
  assert(prv_retained_wfi());assert(CASE==0 || CASE==7 || CASE==13 || CASE==15);
  assert(hw_coordination==0 && scb.SCR==0x10 && !hw_primask);
  assert(irq_low==0x123 && irq_high==0x80000300);
 }else assert((CASE>=1 && CASE<=6) || CASE==14 || CASE>=16);
 assert(wfi_count==((CASE==13 || CASE==14)?0:1));return 0;
}
"""

        def actual_stub(source):
            constants = source[
                source.index("#define SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS") : source.index(
                    "PBL_LOG_MODULE_DEFINE"
                )
            ]
            constants = re.sub(
                r"#define SLEEP_MMIO\(address\).*",
                "#define SLEEP_MMIO(address) (*((address)==SLEEP_AP_IRQ_ENABLE_LOW_ADDRESS?&irq_low:(address)==SLEEP_AP_IRQ_ENABLE_HIGH_ADDRESS?&irq_high:(address)==SLEEP_AP_IRQ_STATUS_LOW_ADDRESS?&status_low:(address)==SLEEP_AP_IRQ_STATUS_HIGH_ADDRESS?&status_high:(address)==SLEEP_AON_COORDINATION_ADDRESS?&hw_coordination:(address)==SLEEP_CORE_CLOCK_MODE_ADDRESS?&hw_mode:(address)==SLEEP_AP_CLOCK_CONTROL_ADDRESS?&hw_ap:(address)==SLEEP_CP_CLOCK_CONTROL_ADDRESS?&hw_cp:&hw_flash))",
                constants,
            )
            return constants + rtc_nvic_endpoints(body(source, "prv_retained_wfi"))

        for case in range(23):
            with self.subTest(case=case):
                run(
                    "soc/t5ai/sleep.c",
                    prefix,
                    main,
                    defines=(f"CASE={case}",)
                    + (
                        ("CONFIG_T5AI_RETAINED_SLEEP_AP_ACTIVE_WAIT=1",)
                        if case in (13, 14)
                        else ()
                    ),
                    transform=actual_stub,
                )

    def test_extended_bounded_return_budget(self):
        basic = "#include <stdbool.h>\n#include <stdint.h>\n"
        run(
            "soc/t5ai/sleep.c",
            basic + HEADER + PREFIX + BLOCKERS,
            MAIN,
            defines=(
                "CASE=9",
                "CONFIG_T5AI_RETAINED_SLEEP_TEST_ENTRY_LIMIT=64",
                "CONFIG_T5AI_RETAINED_SLEEP_TEST_REQUEST_LIMIT=128",
                "EXPECTED_RETURNS=64",
            ),
            transform=transform,
        )

    def test_mask_barrier_completion_requires_zero_wfi_and_balanced_mask(self):
        basic = "#include <stdbool.h>\n#include <stdint.h>\n"
        for case in range(48, 52):
            with self.subTest(case=case):
                run(
                    "soc/t5ai/sleep.c",
                    basic + HEADER + PREFIX + BLOCKERS,
                    MAIN,
                    defines=(
                        f"CASE={case}",
                        "CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL=1",
                    ),
                    transform=transform,
                )

    def test_continuous_ap_only_has_no_slot_or_return_limit(self):
        prefix = PREFIX.replace("#define CONFIG_T5AI_RETAINED_SLEEP_TEST 1", "")
        helpers = MAIN[: MAIN.index("int main(void)")]
        main = (
            helpers
            + r"""
int main(void){
 aon=UINT32_MAX-100u;
 t5ai_sleep_start();worker();assert(s_phase==SleepIdle && !s_stats.fault);
 idle_service(aon+3200);assert(!queued && !arms);
 aon+=SLEEP_START_DELAY_MS*SLEEP_AON_PER_MS-1u;
 idle_service(aon+3200);assert(!queued && !arms);
 ++aon;
 // Pass the old UTC slot and bounded return count without bypassing admission.
 wall=100000;
 for(unsigned i=0;i<256;++i){
  prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
  assert(s_phase==SleepIdle && !s_stats.fault);
 }
 assert(s_stats.entries==256 && tick_resumes==256 && cancels==256);
 assert(cleanup_finishes==256 && s_ap_only_reported);
 // Startup settling must not recur when the AON counter wraps later.
 aon=s_ap_only_started+1u;
 prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 assert(s_phase==SleepIdle && s_stats.entries==257 && s_ap_only_settled);
 assert(!s_stats.cp_entries && !s_stats.cp_resources && !s_stats.cp_clock_cycles && !s_stats.cp_mask_barriers);
#if FORBIDDEN_CP == 1
 ++cp_count;
#elif FORBIDDEN_CP == 2
 ++cp_resources;
#elif FORBIDDEN_CP == 3
 ++cp_clocks;
#elif FORBIDDEN_CP == 4
 ++cp_mask_barriers;
#endif
#if FORBIDDEN_CP
 prepare(aon+3200);assert(enter(aon+3200));idle_service(aon+3200);worker();
 assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_Contract);
#endif
 return 0;
}
"""
        )
        for violation in range(5):
            with self.subTest(violation=violation):
                run(
                    "soc/t5ai/sleep.c",
                    "#include <stdbool.h>\n#include <stdint.h>\n"
                    + HEADER
                    + prefix
                    + BLOCKERS,
                    main,
                    defines=(
                        "CASE=31",
                        "CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY=1",
                        f"FORBIDDEN_CP={violation}",
                    ),
                    transform=lambda source: transform(source).replace(
                        "#define SLEEP_START_DELAY_MS (CASE == 18 ? 30000u : 0u)",
                        "#define SLEEP_START_DELAY_MS 30000u",
                    ),
                )

    def test_ap_only_rejects_each_cp_transition(self):
        basic = "#include <stdbool.h>\n#include <stdint.h>\n"
        for counter in ("cp_count", "cp_resources", "cp_clocks", "cp_mask_barriers"):
            with self.subTest(counter=counter):
                # Inject a CP transition during the real coordinator's AP return.
                prefix = PREFIX.replace(
                    "++ram_entries;", "++ram_entries;++" + counter + ";"
                )
                main = MAIN.replace(
                    "assert(s_phase==SleepTestComplete && !s_stats.cp_entries && !s_stats.fault);",
                    "assert(s_phase==SleepFailed && s_stats.fault==T5AISleepFault_Contract);",
                )
                # A failed contract must stop before requesting a second lease.
                branch_start = main.index("#elif CASE == 31")
                branch_end = main.index("#elif CASE == 10", branch_start)
                branch = main[branch_start:branch_end].replace(
                    "i<SLEEP_TEST_ENTRY_LIMIT", "i<1"
                )
                main = main[:branch_start] + branch + main[branch_end:]
                run(
                    "soc/t5ai/sleep.c",
                    basic + HEADER + prefix + BLOCKERS,
                    main,
                    defines=("CASE=31", "CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY=1"),
                    transform=transform,
                )

    def test_coordinator_and_blockers(self):
        for case in range(48):
            with self.subTest(case=case):
                # Header precedes typed mocks; primitive C headers precede the header.
                basic = "#include <stdbool.h>\n#include <stdint.h>\n"
                run(
                    "soc/t5ai/sleep.c",
                    basic + HEADER + PREFIX + BLOCKERS,
                    MAIN,
                    defines=(f"CASE={case}",)
                    + (
                        ("CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY=1",)
                        if case == 31
                        else ("CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY=1",)
                        if case in (32, 33, 34)
                        else (
                            "CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",
                            "CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF=1",
                        )
                        if case in (40, 41)
                        else ("CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY=1",)
                        if case >= 35
                        else ()
                    ),
                    transform=transform,
                )


if __name__ == "__main__":
    unittest.main()
