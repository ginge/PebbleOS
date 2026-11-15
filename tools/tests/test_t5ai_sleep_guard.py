# SPDX-License-Identifier: Apache-2.0
"""Execute the real sleep safety lease and isolated memory ownership helpers."""

import hashlib
import json
import re
import unittest
from pathlib import Path

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_native_objects import body

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI"
OVERLAY = ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json"


def patched(suffix):
    entry = next(
        e for e in json.loads(OVERLAY.read_text()) if e["path"].endswith(suffix)
    )
    raw = (VENDOR / entry["path"]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == entry["vendor_sha256"]
    source = raw.decode()
    for replacement in entry["replacements"]:
        assert source.count(replacement["old"]) == replacement["count"]
        source = source.replace(replacement["old"], replacement["new"])
    return source


class SleepGuard(unittest.TestCase):
    def test_aon_deadline_admission_and_exact_restore(self):
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static uint32_t hw_lpo,control,reset_control,reason,xtal=2;
#define T5AI_AON_PMU_R41 hw_lpo
#define T5AI_ANALOG_CLOCK_CONTROL xtal
#define T5AI_AON_WDT_CTRL control
#define T5AI_AON_PMU_R2 reset_control
#define T5AI_AON_LPO_SOURCE_MASK 3u
#define T5AI_WDT_RESET_CONTROL_MASK 63u
#define T5AI_WDT_WHOLE_CHIP_RESET_CONTROL 0x26u
#define T5AI_RESET_REASON_SLEEP_RESUME 0x4fu
#define __DSB() ((void)0)
static uint8_t t5ai_reset_reason_get_ap(void){return reason;}
static bool drop_period,drop_reason;
static void t5ai_reset_reason_set_ap(uint8_t r){if(!drop_reason)reason=r;}
static unsigned writes;
void t5ai_watchdog_sleep_guard_restore(void);
static void prv_set_period(uint16_t p){if(!drop_period)control=0xa50000u|p;++writes;}
"""
        main = r"""
int main(void){
 for(unsigned source=0;source<4;++source){
  hw_lpo=source;control=30000;reason=2;reset_control=0x500;
  if(source!=1 && source!=0){assert(!t5ai_watchdog_sleep_guard_arm() && !writes && !s_sleep_guard);continue;}
  assert(t5ai_watchdog_sleep_guard_arm());assert(control==(0xa50000u|1000));
  assert(reason==0x4f && reset_control==(0x500u|0x26));
  assert(!t5ai_watchdog_sleep_guard_arm());
  // CP can change unrelated low-voltage state while this lease is held.
  reset_control|=0x8000;t5ai_watchdog_sleep_guard_restore();
  assert(control==(0xa50000u|30000) && reason==2 && reset_control==0x8500);
  assert(!s_sleep_guard);const unsigned n=writes;
  t5ai_watchdog_sleep_guard_restore();assert(writes==n);writes=0;
 }
 hw_lpo=1u|(1u<<24);assert(!t5ai_watchdog_sleep_guard_arm() && !writes);
 hw_lpo=1;control=0;assert(t5ai_watchdog_sleep_guard_arm());
 t5ai_watchdog_sleep_guard_restore();assert(control==0xa50000u);
 control=123;reason=2;reset_control=0x500;drop_period=true;
 assert(!t5ai_watchdog_sleep_guard_arm() && !s_sleep_guard);
 assert(control==123 && reason==2 && reset_control==0x500);drop_period=false;
 drop_reason=true;
 assert(!t5ai_watchdog_sleep_guard_arm() && !s_sleep_guard);
 assert((control&65535u)==123 && reason==2 && reset_control==0x500);
 return 0;
}
"""
        for pilot in (False, True):
            self._check_guard(prefix, main, pilot)

    def _check_guard(self, prefix, main, pilot):
        run(
            "drivers/watchdog/t5ai.c",
            ("#define CONFIG_T5AI_RETAINED_SLEEP_TEST 1\n" if pilot else "") + prefix,
            main.replace("|1000", "|4000") if pilot else main,
            transform=lambda s: re.sub(
                r"^#define T5AI_ANALOG_CLOCK_CONTROL.*$",
                "",
                s[s.index("// Separate a lost-WFI guard") : s.rindex("#endif")],
                flags=re.MULTILINE,
            ),
        )

    def test_cp_cannot_cancel_or_extend_ap_guard(self):
        source = patched("hal/aon_wdt_hal.c")
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int bk_err_t;
#define BK_OK 0
volatile bool unhygienix_native_sleep_prepared;
static uint32_t period=30000,writes;
static uint32_t aon_wdt_ll_get_wd_period(void){return period;}
static void aon_wdt_ll_set_period(uint32_t p){period=p;++writes;}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            r"""
int main(void){
 assert(!aon_wdt_hal_set_period(9000) && period==9000 && writes==1);
 unhygienix_native_sleep_prepared=true;period=1000;
 assert(!aon_wdt_hal_stop_aon_wdt() && period==1000 && writes==1);
 assert(!aon_wdt_hal_set_period(10000) && period==1000 && writes==1);
 unhygienix_native_sleep_prepared=false;
 assert(!aon_wdt_hal_stop_aon_wdt() && period==0 && writes==2);return 0;
}
""",
            transform=lambda _: re.sub(r"^#include.*$", "", source, flags=re.MULTILINE),
        )

    def test_actual_owner_bitmap_and_heap_cleanup(self):
        pm = patched("bk_pm/pm.c")
        power = patched("pwr_clk/pwr_clk.c")
        functions = "\n".join(
            [
                body(power, "unhygienix_native_psram_app_owned"),
                body(pm, "unhygienix_native_memory_retained"),
                body(pm, "pm_psram_malloc_state_and_power_ctrl"),
            ]
        )
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int bk_err_t;
#define CONFIG_PSRAM 1
#define CONFIG_PSRAM_AS_SYS_MEMORY 1
#define CONFIG_CPU_CNT 1
#define BK_OK 0
#define PM_POWER_PSRAM_MODULE_NAME_APP 9
#define PM_POWER_PSRAM_MODULE_NAME_AS_MEM 1
#define PM_POWER_MODULE_NAME_AHBP 3
#define PM_MODULE_SUB_POWER_DOMAIN_MAX 8
#define PM_POWER_SUB_MODULE_NAME_AHBP_PSRAM 27
#define PM_POWER_MODULE_STATE_OFF 0
#define BK_LOGD(...) ((void)0)
static uint32_t s_pm_psram_ctrl_state,s_pm_ahpb_pm_state,ldo;
static uint32_t used,psram_off,controller_off;
static unsigned s_debug_en;
static uint32_t sys_ll_get_ana_reg13_enpsram(void){return ldo;}
static uint32_t bk_psram_heap_get_used_count(void){return used;}
static void bk_psram_heap_get_used_state(void){}
static void bk_pm_module_vote_psram_ctrl(unsigned module,unsigned state){assert(module==1 && !state);++psram_off;}
static void bk_pm_module_vote_power_ctrl(unsigned module,unsigned state){assert(module==27 && !state);++controller_off;}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            r"""
int main(void){
 ldo=1;s_pm_ahpb_pm_state=8;
 // Any-owner boolean is insufficient: AS_MEM alone is not APP ownership.
 s_pm_psram_ctrl_state=2;assert(!unhygienix_native_memory_retained());
 s_pm_psram_ctrl_state=1u<<9;assert(unhygienix_native_memory_retained());
 assert(!pm_psram_malloc_state_and_power_ctrl() && !psram_off && !controller_off);
 s_pm_ahpb_pm_state=0;assert(!unhygienix_native_memory_retained());
 s_pm_ahpb_pm_state=8;ldo=0;assert(!unhygienix_native_memory_retained());
 // Keep the stock cleanup for genuinely unowned, unused vendor PSRAM.
 s_pm_psram_ctrl_state=0;assert(!pm_psram_malloc_state_and_power_ctrl());
 assert(psram_off==1 && controller_off==1);
 used=1;assert(!pm_psram_malloc_state_and_power_ctrl() && psram_off==1);return 0;
}
""",
            transform=lambda _: functions,
        )

    def test_probe_waits_for_owners_and_never_repeats_a_reset(self):
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#define PBL_SECTION_RAM
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_DBG(...) ((void)0)
#define PBL_LOG_ERR(...) (++errors)
#define T5AI_RESET_REASON_SLEEP_RESUME 0x4fu
#define CONFIG_T5AI_SLEEP_GUARD_TEST_AT_UTC (CASE==9 ? 0u : 1000u)
typedef int32_t time_t;
static time_t wall=1000;
static time_t pbl_realtime(void){return wall;}
#define TIMER_INVALID_ID 0u
#define PBL_MSEC(ms) (ms)
struct pbl_work {void (*fn)(struct pbl_work *);};
struct pbl_delayed_work {struct pbl_work work;};
static int pbl_sys_workq_high;
typedef struct {uint32_t owners;bool faulted;} T5AISleepBlockers;
static unsigned errors,created,deleted,starts,locks,hw_mask,armed,last_delay;
typedef int status_t;
#define S_SUCCESS 0
#define T5AI_PM_NATIVE_ALARM_MAX_TICKS 16000u
static unsigned lease_arms,lease_cancels;
static int t5ai_native_alarm_init(void){return CASE==12 ? -1 : 0;}
static int t5ai_native_alarm_prepare_sleep(uint32_t ticks,uint32_t *gen){assert(ticks==16000);++lease_arms;*gen=17;return 0;}
static int t5ai_native_alarm_cancel(uint32_t gen){assert(gen==17);++lease_cancels;return 0;}

static bool quiet=true,guard_ok=true,faulted,drop,channel_ready=true;
static uint8_t boot;
static void (*scheduled)(struct pbl_work *);
static void (*queued)(void *);
static jmp_buf sleeping;
#define __disable_irq() (hw_mask=1)
#define __get_PRIMASK() hw_mask
#define __set_PRIMASK(value) (hw_mask=(value))
#define __DSB() ((void)0)
static void test_wfi(void){assert(hw_mask && locks==1 && armed==1);longjmp(sleeping,1);}
#define __WFI() test_wfi()
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){assert(locks==1);--locks;}
static uint8_t t5ai_reset_reason_get_boot_ap(void){return boot;}
static bool t5ai_watchdog_sleep_guard_arm(void){assert(hw_mask && locks==1);++armed;return guard_ok;}
static bool t5ai_mailbox_channel_sleep_ready(void){return quiet && channel_ready;}
static bool t5ai_mb_ipc_sleep_ready(void){return quiet;}
static bool t5ai_hci_sleep_ready(void){return quiet;}
static T5AISleepBlockers t5ai_sleep_blockers(void){assert(hw_mask && locks==1);return (T5AISleepBlockers){.owners=quiet?0:1,.faulted=faulted};}
static void pbl_dcache_flush_range(const void *p,unsigned n){assert(p && n==8);if(CASE==15)wall=1002;if(CASE==16)wall=999;}
static void pbl_delayed_work_init(struct pbl_delayed_work *t,void (*fn)(struct pbl_work *)){++created;t->work.fn=fn;}
static bool pbl_delayed_work_cancel(struct pbl_delayed_work *t){++deleted;return true;}
static int pbl_delayed_work_reschedule(int *q,struct pbl_delayed_work *t,unsigned ms){
 assert(q==&pbl_sys_workq_high && (ms==30000 || ms==1000 || ms==20));scheduled=t->work.fn;last_delay=ms;++starts;return 0;
}
static bool pbl_sys_work_try_call(void (*fn)(void *),void *ctx){
 assert(!ctx);if(drop)return false;queued=fn;return true;
}
"""
        main = r"""
static void fire(void){assert(scheduled);scheduled(NULL);if(queued){void (*fn)(void *)=queued;queued=NULL;fn(NULL);}}
int main(void){
#if CASE == 8 || CASE == 9
 wall=1002;t5ai_sleep_guard_test_start();assert(!created && !armed);return 0;
#endif
#if CASE == 6
 s_guard_attempt.magic=GUARD_ATTEMPT_MAGIC;s_guard_attempt.inverse=~GUARD_ATTEMPT_MAGIC;
 t5ai_sleep_guard_test_start();assert(!created && !armed);return 0;
#elif CASE == 7
 s_guard_attempt.magic=GUARD_ATTEMPT_MAGIC;s_guard_attempt.inverse=0;
#endif
#if CASE == 0
 boot=0x4f;t5ai_sleep_guard_test_start();assert(!created && !armed);return 0;
#endif
 t5ai_sleep_guard_test_start();t5ai_sleep_guard_test_start();assert(created==1 && starts==1);
#if CASE == 5
 drop=true;fire();assert(deleted==1 && errors==1 && !armed);return 0;
#endif
#if CASE == 17
 wall=1002;fire();assert(!lease_arms && !armed && deleted==1);return 0;
#endif
#if CASE == 12
 fire();assert(!lease_arms && !armed && deleted==1 && errors==1);return 0;
#endif
 fire();assert(starts==2 && !armed && !locks && lease_arms==1); // logging drains before final idle check
#if CASE == 10
 wall=999;for(unsigned i=0;i<180;++i)fire();assert(!armed && deleted==1 && !errors && lease_cancels==1);
#elif CASE == 11
 wall=1002;fire();assert(!armed && deleted==1 && !errors && lease_cancels==1);
#elif CASE == 1
 quiet=false;for(unsigned i=0;i<GUARD_TEST_ATTEMPTS;++i)fire();assert(!armed && deleted==1 && errors==1 && lease_cancels==1);
#elif CASE == 2
 guard_ok=false;fire();assert(armed==1 && deleted==1 && errors==1);
#elif CASE == 3
 if(!setjmp(sleeping)){fire();assert(false);}else {assert(s_guard_attempt.magic==GUARD_ATTEMPT_MAGIC && s_guard_attempt.inverse==~GUARD_ATTEMPT_MAGIC);return 0;}
#elif CASE == 4
 faulted=true;fire();assert(!armed && deleted==1 && errors==1);
#elif CASE == 13
 quiet=false;fire();assert(last_delay==20 && !armed && starts==3);
 assert(s_guard_seen_owners==1 && !s_guard_last_ready);
 quiet=true;if(!setjmp(sleeping)){fire();assert(false);}else {assert(armed==1 && !lease_cancels);return 0;}
#elif CASE == 14
 channel_ready=false;fire();assert(last_delay==20 && !armed && !s_guard_last_owners);
 assert(s_guard_last_ready==(GuardReady_Ipc|GuardReady_Hci));
 wall=1002;fire();assert(!armed && deleted==1 && lease_cancels==1);
#elif CASE == 15 || CASE == 16
 fire();assert(!armed && deleted==1 && lease_cancels==1 && !hw_mask && !locks);
#endif
 assert(!hw_mask && !locks);return 0;
}
"""
        for case in range(18):
            with self.subTest(case=case):
                run(
                    "soc/t5ai/sleep_guard_test.c",
                    prefix,
                    main,
                    defines=(f"CASE={case}",),
                    transform=lambda s: re.sub(
                        r"#define GUARD_READ\(address\).*",
                        "#define GUARD_READ(address) 0u",
                        s,
                    ),
                )

    def test_nmi_hal_cannot_overwrite_the_aon_lease(self):
        source = patched("hal/wdt_hal.c")
        source = source.replace('__attribute__((section(".itcm_sec_code")))', "")
        functions = "\n".join(
            body(source, name)
            for name in ["wdt_hal_close", "wdt_hal_force_feed", "wdt_hal_close_unused"]
        )
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CONFIG_AON_WDT 1
#define SOC_AON_WDT_REG_BASE 1u
#define SOC_WDT_REG_BASE 100u
#define AON_WDT_ID 0u
#define NMI_WDT_ID 1u
typedef unsigned wdt_unit_t;
static volatile bool unhygienix_native_sleep_prepared;
static uint32_t period=30000,aon_writes,nmi_writes;
static void write_register(unsigned address,unsigned value){
 if(address==SOC_AON_WDT_REG_BASE){period=value&65535u;++aon_writes;}else ++nmi_writes;
}
#define REG_WRITE(address,value) write_register(address,value)
#define REG_SET(...) (++nmi_writes)
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            r"""
int main(void){
 unhygienix_native_sleep_prepared=true;period=1000;
 wdt_hal_close_unused(NMI_WDT_ID);assert(period==1000 && !aon_writes);
 wdt_hal_force_feed();assert(period==1000 && !aon_writes && nmi_writes);
 wdt_hal_close();assert(period==1000 && !aon_writes);
 unhygienix_native_sleep_prepared=false;
 wdt_hal_close_unused(NMI_WDT_ID);assert(period==0 && aon_writes==2);
 wdt_hal_force_feed();assert(period==65532 && aon_writes==4);return 0;
}
""",
            transform=lambda _: functions,
        )
