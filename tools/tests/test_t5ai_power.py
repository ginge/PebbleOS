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
static struct { uint32_t CTRL, LOAD; } s_systick;
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
