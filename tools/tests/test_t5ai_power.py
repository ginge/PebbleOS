# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

STUB_HEADERS = {
    "pbl/soc/t5ai/power.h": """
#include <stdint.h>
#include <stdbool.h>
void t5ai_power_init(void);
void t5ai_power_update_clock(void);
void t5ai_power_sync_tick_clock(void);
""",
    "pbl/soc/t5ai/cp_mailbox.h": """
#include <stdbool.h>
typedef int status_t;
#define S_SUCCESS 0
typedef enum {T5AICpuFrequency_120MHz=3,T5AICpuFrequency_240MHz=4} T5AICpuFrequency;
status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency);
""",
    "pbl/soc/t5ai/freertos.h": """
void t5ai_freertos_apply_systick_clock(void);
""",
    "pbl/logging/logging.h": """
#define PBL_LOG_MODULE_DEFINE(a,b)
#define PBL_LOG_DBG(...)
#define PBL_LOG_ERR(...)
""",
    "system/passert.h": """
#include <assert.h>
#define PBL_ASSERTN(x) assert(x)
""",
    "FreeRTOS.h": """
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdPASS 1
#define configMAX_PRIORITIES 8
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
""",
    "semphr.h": """
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t,uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
""",
    "task.h": """
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
""",
    "cmsis_core.h": """
extern uint32_t SystemCoreClock;
static struct { uint32_t CTRL, LOAD, VAL; } s_systick;
#define SysTick (&s_systick)
#define SysTick_CTRL_ENABLE_Msk 1u
#define CONFIG_KERNEL_TICK_HZ 1000u

""",
    "system/profiler.h": """
#define PROFILER_INIT
#define PROFILER_START
""",
}


class T5AIPowerTest(unittest.TestCase):
    def test_board_startup_elects_clock(self):
        # Compile the real board startup function with the real power driver.
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        start = board.index("void board_init(void) {")
        end = board.index("\n}", start) + 2
        startup = board[start:end]
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include T5AI_POWER_SOURCE
uint32_t SystemCoreClock;
static unsigned votes, stage;
#define FAILED(s) ((s) < 0)
#define PBL_ASSERTN(s) assert(s)
#define I2C0_BUS 0

static status_t t5ai_cp_mailbox_vote_app_sleep(bool allow) {
  assert(!allow && stage == 1); stage = 2; return S_SUCCESS;
}
status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency frequency) {
  assert(frequency == T5AICpuFrequency_240MHz && stage == 2);
  ++votes;
  *(volatile uint32_t *)0x44010020u = 0x31u;
  *(volatile uint32_t *)0x44010014u = 16u;
  return S_SUCCESS;
}
static status_t t5ai_cp_mailbox_enable_psram(void) {
  assert(votes == 1 && SystemCoreClock == 240000000u);
  assert(SysTick->LOAD == 239999u && SysTick->VAL == 0u);
  stage = 3; return S_SUCCESS;
}
static bool prv_validate_framebuffer_ram(void) { return stage == 3; }
BOARD_STARTUP
int main(void) {
  assert(mmap((void *)0x44010000u, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
  *(volatile uint32_t *)0x44010020u = 0x37u;
  *(volatile uint32_t *)0x44010014u = 16u;
  SystemCoreClock = 60000000u;
  SysTick->CTRL = 1u; SysTick->LOAD = 59999u; SysTick->VAL = 17u;
  stage = 1; // Device registry has initialized the I2C parent.
  board_init();
  assert(votes == 1 && stage == 3 && s_initialized);
  return 0;
}
""".replace("BOARD_STARTUP", startup)
        self.run_harness(harness)

    def run_harness(self, source):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in STUB_HEADERS.items():
                header = directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(contents)
            (directory / "startup.c").write_text(source)
            executable = directory / "startup"
            subprocess.run([
                "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory),
                f'-DT5AI_POWER_SOURCE="{ROOT / "soc/t5ai/power.c"}"',
                str(directory / "startup.c"), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)

    def test_active_clock_election(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in STUB_HEADERS.items():
                header = directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(contents)
            executable = directory / "test_power"
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(directory),
                    f'-DT5AI_POWER_SOURCE="{ROOT / "soc/t5ai/power.c"}"',
                    str(ROOT / "boards/unhygienix/tuya/tests/test_t5ai_power.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
