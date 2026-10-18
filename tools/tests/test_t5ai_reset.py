# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HEADERS = {
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
#define __get_BASEPRI() 0
""",
    "pbl/soc/t5ai/state_trace.h": """
#pragma once
#include <stdint.h>
static inline void t5ai_state_trace_freeze(uint32_t reason) { (void)reason; }
""",
    "pbl/kernel/kernel.h": """
#pragma once
static inline int pbl_kernel_is_running(void) { return 1; }
""",
    "pbl/mcu/interrupts.h": """
#pragma once
#include <cmsis_core.h>
static inline int mcu_state_is_isr(void) { return 0; }
""",
    "pbl/logging/logging.h": """
#pragma once
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_WRN(...) ((void)0)
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
unsigned reports;
#include WATCHDOG_SOURCE
#include REBOOT_SOURCE
#ifdef CONFIG_T5AI_REBOOT_TRACE
static uint32_t last_reset_event;
void t5ai_reboot_trace_watchdog(bool running) { (void)running; }
void t5ai_reboot_trace_event(T5RebootTraceEvent event, uint32_t value) {
  if (event == T5RebootTrace_ForceReset) last_reset_event = value;
}
#endif
static uint32_t retained[32];
uint32_t t5ai_retained_read(uint8_t index) { return retained[index]; }
void t5ai_retained_write(uint8_t index, uint32_t value) { retained[index] = value; }
static void boot(uint8_t ap, uint8_t cp) {
  memset(retained, 0, sizeof(retained));
  s_boot_reason_pending = true;
  s_initialized = false;
  s_boot_captured = false;
  reports = 0;
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
#include <stdint.h>
#include <setjmp.h>
typedef uint32_t pbl_tick_t;
typedef struct { uint32_t ticks; } pbl_timeout_t;
#define CP_HEARTBEAT_PERIOD_MS 1000u
#define PBL_TICKS(t) ((pbl_timeout_t){.ticks=(t)})
#define PBL_MSEC(t) PBL_TICKS(t)
#define pbl_ms_to_ticks(t) (t)
static uint32_t now, delay, calls, sleeps;
static jmp_buf finished;
static inline uint32_t pbl_uptime_ticks(void) { return now; }
static void t5ai_cp_mailbox_heartbeat(void) { ++calls; now += delay; }
static void pbl_thread_sleep(pbl_timeout_t timeout) {
  assert(timeout.ticks > 0 && timeout.ticks <= CP_HEARTBEAT_PERIOD_MS);
  now += timeout.ticks + delay; // Late wakeups as well as delayed sends.
  if (++sleeps == 5) longjmp(finished, 1);
}
FUNCTION
int main(void) {
  const uint32_t delays[] = {0, 999, 1000, 1001, 6300};
  const uint32_t starts[] = {0, UINT32_MAX - 500};
  for (unsigned i = 0; i < sizeof(delays)/sizeof(delays[0]); ++i) {
    for (unsigned j = 0; j < sizeof(starts)/sizeof(starts[0]); ++j) {
      now = starts[j]; delay = delays[i]; calls = sleeps = 0;
      if (!setjmp(finished)) prv_heartbeat_task(0);
      assert(calls == 5 && sleeps == 5);
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
                ))

    def test_reset_reason_without_diagnostics(self):
        self.compile_run(RESET_HARNESS, HEADERS, (
            "-DCONFIG_WATCHDOG=1", "-DCONFIG_SOC_T5AI=1",
            "-DCONFIG_WATCHDOG_TIMEOUT_MS=30000",
            f'-DWATCHDOG_SOURCE="{ROOT / "drivers/watchdog/t5ai.c"}"',
            f'-DREBOOT_SOURCE="{ROOT / "soc/t5ai/reboot_reason.c"}"',
        ))

    def test_heartbeat_late_execution_and_tick_wrap(self):
        source = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        start = source.index("static void prv_heartbeat_task(void *context) {")
        end = source.index("\nstatic status_t prv_pm_request", start)
        self.compile_run(HEARTBEAT_HARNESS.replace("FUNCTION", source[start:end]))


if __name__ == "__main__":
    unittest.main()
