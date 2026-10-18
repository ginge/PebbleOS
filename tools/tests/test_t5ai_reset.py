# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HEADERS = {
    "pbl/kernel/sched.h": '#pragma once\n#include <pbl/kernel/kernel.h>\n',
    "pbl/kernel/types.h": '#pragma once\n',

    "pbl/kernel/compiler.h": """
#pragma once
#define PBL_PACKED __attribute__((packed))
#define PBL_NORETURN __attribute__((noreturn))
""",
    "cmsis_core.h": """
#pragma once
#define __disable_irq() ((void)0)
#define __DSB() ((void)0)
void test_wfi(void);
#define __WFI() test_wfi()
extern unsigned test_basepri;
#define __get_BASEPRI() test_basepri
""",
    "pbl/soc/t5ai/state_trace.h": """
#pragma once
#include <stdint.h>
static inline void t5ai_state_trace_freeze(uint32_t reason) { (void)reason; }
""",
    "pbl/kernel/kernel.h": """
#pragma once
extern unsigned test_kernel_running;
static inline int pbl_kernel_is_running(void) { return test_kernel_running; }
""",
    "pbl/mcu/interrupts.h": """
#pragma once
#include <cmsis_core.h>
extern unsigned test_isr;
static inline int mcu_state_is_isr(void) { return test_isr; }
""",
    "pbl/logging/logging.h": """
#pragma once
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_WRN(...) (++warnings)
extern unsigned warnings;
#define PBL_LOG_INFO(...) (++reports)
extern unsigned reports;
""",
}

RESET_HARNESS = r"""
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>
#include <setjmp.h>
static jmp_buf reset_done;
void test_wfi(void) { longjmp(reset_done, 1); }
unsigned reports, warnings;
unsigned test_basepri, test_isr, test_kernel_running = 1;
#include WATCHDOG_SOURCE
#include REBOOT_SOURCE
#include COMMON_REBOOT_SOURCE
#ifdef CONFIG_T5AI_REBOOT_TRACE
static uint32_t last_reset_event;
void t5ai_reboot_trace_watchdog(bool running) { (void)running; }
void t5ai_reboot_trace_event(T5RebootTraceEvent event, uint32_t value) {
  if (event == T5RebootTrace_ForceReset) last_reset_event = value;
}
#endif
#ifdef RETAINED_SOURCE
#include RETAINED_SOURCE
#else
static uint32_t retained[32];
uint32_t t5ai_retained_read(uint8_t index) { return retained[index]; }
void t5ai_retained_write(uint8_t index, uint32_t value) { retained[index] = value; }
#endif
static void boot(uint8_t ap, uint8_t cp) {
#ifdef RETAINED_SOURCE
  memset(&s_retained, 0, sizeof(s_retained));
  t5ai_retained_init();
#else
  memset(retained, 0, sizeof(retained));
#endif
  s_boot_reason_pending = true;
  s_initialized = false;
  s_boot_captured = false;
  reports = warnings = test_basepri = test_isr = 0;
  test_kernel_running = 1;
  // CP has consumed the PMU fields; its shared copies hold decoded causes.
  T5AI_AON_PMU_R0 = 0;
  *(volatile uint32_t *)0x440001e8u = 3;
  T5AI_AP_SAVED_RESET_REASON = ap;
  T5AI_CP_SAVED_RESET_REASON = cp;
  assert(t5ai_reset_reason_get_boot_ap() == ap);
  assert(t5ai_reset_reason_get_boot_cp() == cp);
  assert(t5ai_reset_reason_get_ap() == 0);
}
int main(void) {
  assert(mmap((void *)0x44000000u, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  assert(mmap((void *)0x2809f000u, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  assert(mmap((void *)0x44800000u, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  RebootReason reason;
  boot(2, 0x15);
  assert(watchdog_check_reset_flag());
  assert(watchdog_clear_reset_flag().independent_watchdog_reset);
  assert(!watchdog_get_reset_flag().independent_watchdog_reset);
  assert(t5ai_reset_reason_get_boot_ap() == 2);
  T5AI_AP_SAVED_RESET_REASON = 0; T5AI_CP_SAVED_RESET_REASON = 0;
  t5ai_reset_reason_set_ap(0x71);
  assert(t5ai_reset_reason_get_boot_ap() == 2);
  assert(t5ai_reset_reason_get_boot_cp() == 0x15);
  // A current-runtime marker cannot change the cause of the previous boot.
  boot(0, 0);
  watchdog_start();
  assert(T5AI_AON_WDT_CTRL == ((0xa5u << 16) | CONFIG_WATCHDOG_TIMEOUT_MS));
  assert(!watchdog_check_reset_flag());
  watchdog_feed();
  assert(T5AI_AON_WDT_CTRL == ((0xa5u << 16) | CONFIG_WATCHDOG_TIMEOUT_MS));
  watchdog_stop();
  assert(T5AI_AON_WDT_CTRL == (0xa5u << 16));
  watchdog_feed(); // A stopped watchdog must remain disabled.
  assert(T5AI_AON_WDT_CTRL == (0xa5u << 16));
  boot(T5AI_RESET_REASON_PEBBLE_BASE | RebootReasonCode_Assert, 0x15);
  reboot_reason_get(&reason);
  assert(reason.code == RebootReasonCode_Assert);
  reboot_reason_clear();
  assert(reports == 1);
  reboot_reason_get(&reason);
  assert(reason.code == RebootReasonCode_Unknown);
  reboot_reason_clear();
  assert(reports == 1);
  // Retained detail wins over the fallback snapshot.
  boot(T5AI_RESET_REASON_PEBBLE_BASE | RebootReasonCode_Assert, 0);
  reason = (RebootReason){.code = RebootReasonCode_RemoteReset};
  reason.extra.value = 123;
  reboot_reason_set(&reason);
  reboot_reason_get(&reason);
  assert(reason.code == RebootReasonCode_RemoteReset && reason.extra.value == 123);
  boot(T5AI_RESET_REASON_MAILBOX_BASE | 3, 0);
  reboot_reason_get(&reason);
  assert(reason.code == RebootReasonCode_MailboxTimeout && reason.extra.value == 3);
  boot(0, 0x15);
  reboot_reason_get(&reason);
  assert(reason.code == RebootReasonCode_Unknown); // CP cause is not an AP fault.
  assert(!watchdog_check_reset_flag());
  // Preserve all four words and the first hardware marker in fault contexts.
  boot(0, 0);
  RebootReason first = {.code = RebootReasonCode_HardFault, .data16 = 0xbeef};
  first.hard_fault.pc = 0x12345678;
  first.hard_fault.lr = 0x87654321;
  first.hard_fault.xpsr = 0x0100000b;
  reboot_reason_set(&first);
  const uint8_t first_marker = t5ai_reset_reason_get_ap();
  RebootReason later = {.code = RebootReasonCode_Watchdog};
  test_isr = 1;
  reboot_reason_set(&later);
  test_isr = 0; test_basepri = 1;
  reboot_reason_set(&later);
  test_basepri = 0; test_kernel_running = 0;
  reboot_reason_set(&later);
  assert(warnings == 0);
  test_kernel_running = 1;
  reboot_reason_set(&later);
  assert(warnings == 1);
  reboot_reason_get(&reason);
  assert(memcmp(&reason, &first, sizeof(reason)) == 0);
  assert(t5ai_reset_reason_get_ap() == first_marker);
  reboot_reason_set_restarted_safely();
  first.restarted_safely = true;
  reboot_reason_get(&reason);
  assert(memcmp(&reason, &first, sizeof(reason)) == 0);
  reboot_set_slot_of_last_launched_app(123);
  reboot_reason_clear();
  reboot_reason_get(&reason);
  RebootReason empty = {0};
  assert(memcmp(&reason, &empty, sizeof(reason)) == 0);
  assert(reboot_get_slot_of_last_launched_app() == 123);
  assert(t5ai_reset_reason_get_ap() == 0);
  const uint8_t existing[] = {0, 2, 0x4f, 0x50, 0x53, 0x5f, 0x60, 0x61,
      0x70, 0x71, 0x78, 0x60 + RebootReasonCode_MailboxTimeout, 0x7f};
  for (unsigned i=0; i<sizeof(existing); ++i) {
    uint8_t value = existing[i];
    T5AI_AON_PMU_R0 = 0x00812343; // Preserve CP/PMU fields outside AP reason.
    t5ai_reset_reason_set_ap(value);
    if (!setjmp(reset_done)) t5ai_force_whole_chip_reset(T5AI_RESET_REASON_PEBBLE_BASE);
    bool valid = (value >= 0x50 && value < 0x60) ||
        (value > 0x60 && value <= 0x60 + RebootReasonCode_MailboxTimeout);
    assert(t5ai_reset_reason_get_ap() == (valid ? value : 0x60));
#ifdef CONFIG_T5AI_REBOOT_TRACE
    assert(last_reset_event == (valid ? value : 0x60));
#endif
    assert((T5AI_AON_PMU_R0 & ~T5AI_AP_RESET_REASON_MASK) == 0x00812343);
    assert(T5AI_AON_WDT_CTRL == ((0xa5u << 16) | 10u));
    assert(T5AI_NMI_WDT_PERIOD == ((0xa5u << 16) | 10u));
  }
  // A specific caller-supplied reason takes precedence over an existing marker.
  t5ai_reset_reason_set_ap(0x71);
  if (!setjmp(reset_done)) t5ai_force_whole_chip_reset(0x53);
  assert(t5ai_reset_reason_get_ap() == 0x53);
  return 0;
}
"""

HEARTBEAT_HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t pbl_tick_t;
#define CP_HEARTBEAT_PERIOD_MS 1000u
#define PBL_TICK_FOREVER UINT32_MAX
#define pbl_ms_to_ticks(t) (t)
static uint64_t now,s_next_heartbeat_ticks;
static bool s_runtime_initialized;
static unsigned calls,delay;
static uint64_t pbl_uptime_ticks(void) { return now; }
static void prv_heartbeat(void) { ++calls; now += delay; }
FUNCTION
int main(void) {
  const unsigned delays[] = {0, 999, 1000, 1001, 6300};
  const uint64_t starts[] = {0, UINT32_MAX - 500ull, (1ull<<32) + 123};
  for (unsigned i = 0; i < sizeof(delays)/sizeof(delays[0]); ++i) {
    for (unsigned j = 0; j < sizeof(starts)/sizeof(starts[0]); ++j) {
      now = starts[j]; delay = delays[i]; calls = 0;
      s_next_heartbeat_ticks=0;s_runtime_initialized=false;
      assert(t5ai_cp_mailbox_heartbeat_poll()==PBL_TICK_FOREVER && !calls);
      s_runtime_initialized=true;
      for(unsigned n=0;n<5;++n){
        assert(t5ai_cp_mailbox_heartbeat_poll()==1000 && calls==n+1);
        assert(t5ai_cp_mailbox_heartbeat_poll()==1000 && calls==n+1);
        now+=999;
        assert(t5ai_cp_mailbox_heartbeat_poll()==1 && calls==n+1);
        now+=1+delay;
      }
    }
  }
  return 0;
}
"""

class T5AIResetTest(unittest.TestCase):
    def compile_run(self, source, headers=None, flags=()):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in (headers or {}).items():
                header = directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(contents)
            harness = directory / "test.c"
            harness.write_text(source)
            executable = directory / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-address-of-packed-member", "-I", str(directory),
                 "-I", str(ROOT / "include"), "-I", str(ROOT / "fw"),
                 *flags, str(harness), "-o", str(executable)], check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_boot_snapshot_and_configured_watchdog_period(self):
        for period in (1, 1234, 30000, 65535):
            with self.subTest(period=period):
                self.compile_run(RESET_HARNESS, HEADERS, (
                    "-DCONFIG_WATCHDOG=1", "-DCONFIG_SOC_T5AI=1", "-DCONFIG_T5AI_REBOOT_TRACE=1",
                    f"-DCONFIG_WATCHDOG_TIMEOUT_MS={period}",
                    f'-DWATCHDOG_SOURCE="{ROOT / "drivers/watchdog/t5ai.c"}"',
                    f'-DREBOOT_SOURCE="{ROOT / "soc/t5ai/reboot_reason.c"}"',
            f'-DCOMMON_REBOOT_SOURCE="{ROOT / "fw/system/reboot_reason.c"}"',
                ))

    def test_reset_reason_without_diagnostics(self):
        self.compile_run(RESET_HARNESS, HEADERS, (
            "-DCONFIG_WATCHDOG=1", "-DCONFIG_SOC_T5AI=1",
            "-DCONFIG_WATCHDOG_TIMEOUT_MS=30000",
            f'-DWATCHDOG_SOURCE="{ROOT / "drivers/watchdog/t5ai.c"}"',
            f'-DREBOOT_SOURCE="{ROOT / "soc/t5ai/reboot_reason.c"}"',
            f'-DCOMMON_REBOOT_SOURCE="{ROOT / "fw/system/reboot_reason.c"}"',
        ))

    def test_heartbeat_late_execution_and_tick_wrap(self):
        source = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        start = source.index("pbl_tick_t t5ai_cp_mailbox_heartbeat_poll(void) {")
        end = source.index("\nstatic status_t prv_pm_request", start)
        self.compile_run(HEARTBEAT_HARNESS.replace("FUNCTION", source[start:end]))


if __name__ == "__main__":
    unittest.main()
