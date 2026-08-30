# SPDX-License-Identifier: Apache-2.0
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location(
    "check_tuya_kernel", ROOT / "tools/check_tuya_kernel.py"
)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class TuyaKernelCheckTest(unittest.TestCase):
    def test_t5ai_fpu_is_enabled_during_reset_initialization(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        disable_irq = initialization.index("__disable_irq()")
        enable_fpu = initialization.index("SCB->CPACR |=")
        relocate_vectors = initialization.index("SCB->VTOR =")
        enable_irq = initialization.index("__enable_irq()")
        self.assertLess(disable_irq, enable_fpu)
        self.assertLess(enable_fpu, relocate_vectors)
        self.assertLess(relocate_vectors, enable_irq)
        self.assertIn("(3u << 20u) | (3u << 22u)", initialization)

    def test_t5ai_does_not_inherit_bootloader_scheduler_state(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        disable_irq = initialization.index("__disable_irq()")
        stop_systick = initialization.index("SysTick->CTRL = 0u")
        clear_pending = initialization.index("SCB_ICSR_PENDSTCLR_Msk")
        align_stack = initialization.index("SCB_CCR_STKALIGN_Msk")
        enable_irq = initialization.index("__enable_irq()")
        self.assertLess(disable_irq, stop_systick)
        self.assertLess(stop_systick, clear_pending)
        self.assertLess(clear_pending, align_stack)
        self.assertLess(align_stack, enable_irq)
        self.assertIn("SysTick->LOAD = 0u", initialization)
        self.assertIn("SysTick->VAL = 0u", initialization)
        self.assertIn("SCB_ICSR_PENDSVCLR_Msk", initialization)

    def test_t5ai_core_clock_is_an_explicit_systick_contract(self):
        kconfig = (ROOT / "soc/Kconfig").read_text()
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        port = (ROOT / "soc/t5ai/freertos.c").read_text()
        self.assertIn("config T5AI_CORE_CLOCK_HZ", kconfig)
        self.assertIn("default 120000000", kconfig)
        self.assertIn(
            "SystemCoreClock = CONFIG_T5AI_CORE_CLOCK_HZ", initialization
        )
        self.assertIn(
            "CONFIG_T5AI_CORE_CLOCK_HZ % RTC_TICKS_HZ == 0u", port
        )
        self.assertIn("T5AI_SYSTICK_RELOAD <= SysTick_LOAD_RELOAD_Msk", port)

    def test_cmsis_priorities_do_not_use_raw_freertos_bytes(self):
        config = (ROOT / "third_party/freertos/FreeRTOSConfig.h").read_text()
        fault = (ROOT / "src/fw/kernel/fault_handling.c").read_text()
        watchdog = (ROOT / "src/fw/drivers/task_watchdog.c").read_text()
        self.assertIn(
            "configMAX_SYSCALL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS)",
            config,
        )
        self.assertIn("configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY", fault)
        self.assertIn("configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY", watchdog)
        self.assertNotIn(
            "NVIC_SetPriority(MemoryManagement_IRQn, "
            "configMAX_SYSCALL_INTERRUPT_PRIORITY)",
            fault,
        )
        self.assertNotIn(
            "NVIC_SetPriority(WATCHDOG_FREERTOS_IRQn, "
            "configMAX_SYSCALL_INTERRUPT_PRIORITY)",
            watchdog,
        )

    def test_t5ai_scales_raw_aon_ticks_at_the_rtc_boundary(self):
        interface = (ROOT / "include/pbl/drivers/rtc.h").read_text()
        implementation = (ROOT / "src/fw/drivers/rtc/t5ai.c").read_text()
        t5ai_interface = interface.split("#elif defined(CONFIG_SOC_T5AI)", 1)[1]
        t5ai_interface = t5ai_interface.split("#else", 1)[0]
        self.assertIn("#define RTC_TICKS_HZ (1000u)", t5ai_interface)
        self.assertIn("#define T5AI_AON_RTC_HZ 32000u", implementation)
        self.assertIn(
            "return prv_raw_ticks() / (T5AI_AON_RTC_HZ / RTC_TICKS_HZ);",
            implementation,
        )
        self.assertIn("elapsed / T5AI_AON_RTC_HZ", implementation)

    def test_only_expected_config_is_rewritten(self):
        config = "#define configENABLE_MPU                   0\n#define OTHER 7\n"
        for enabled in (0, 1):
            result = checker.make_config(config, enabled)
            self.assertIn(f"#define configENABLE_MPU {enabled}\n", result)
            self.assertIn("#define configTOTAL_MPU_REGIONS 16\n", result)
            self.assertIn("#define OTHER 7", result)
        with self.assertRaises(ValueError):
            checker.make_config("#define configENABLE_MPU 1\n", 1)
        with self.assertRaises(ValueError):
            checker.make_config(config + config, 1)

    def test_pebble_config_reserves_tls(self):
        config = "#define configENABLE_MPU 0\n" + (
            "#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 3\n" * 2
        )
        result = checker.make_config(config, 1, True)
        self.assertEqual(result.count("configNUM_THREAD_LOCAL_STORAGE_POINTERS 5"), 2)
        self.assertIn("#define configPEBBLE_SYSCALL_BRIDGE 1", result)
        self.assertIn("#define configPEBBLE_CORE_ID 1", result)
        for bad in (
            config.replace("POINTERS 3", "POINTERS 4"),
            "#define configENABLE_MPU 0\n",
        ):
            with self.assertRaises(ValueError):
                checker.make_config(bad, 1, True)
        with self.assertRaises(ValueError):
            checker.make_config(config, 0, True)

    def test_svc_dispatch_policy(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "test-svc"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DPBL_RTOS_TUYA=1",
                    f"-I{ROOT / 'include'}",
                    f"-I{checker.SOURCE / 'kernel'}",
                    str(checker.SOURCE / "kernel/svc_bridge.c"),
                    str(checker.SOURCE / "tests/test_svc_bridge.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_task_memory_policy(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "test-memory"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'include'}",
                    f"-I{ROOT / 'src/fw'}",
                    f"-I{checker.SOURCE / 'kernel'}",
                    str(checker.SOURCE / "kernel/task_memory.c"),
                    str(checker.SOURCE / "kernel/task_memory_pebble.c"),
                    str(checker.SOURCE / "tests/test_task_memory.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_compiler_outputs_are_redirected_without_a_shell(self):
        entry = {
            "command": "'/sdk/tool chain/gcc' -DNAME='a;b' -I/sdk -MD -MF old.d -MT old.o -o old.o -c old.c"
        }
        result = checker.compile_command(
            entry, Path("/copy/tasks.c"), Path("/out/tasks.o"), [Path("/config")]
        )
        self.assertEqual(result[0:2], ["/sdk/tool chain/gcc", "-I/config"])
        self.assertIn("-DNAME=a;b", result)
        self.assertEqual(result[result.index("-c") + 1], "/copy/tasks.c")
        self.assertEqual(result[result.index("-o") + 1], "/out/tasks.o")
        self.assertEqual(result[result.index("-MF") + 1], "/out/tasks.d")
        self.assertEqual(result[result.index("-MT") + 1], "/out/tasks.o")

    def test_rejects_non_compiler_commands(self):
        for args in (["gcc", "x.c"], ["gcc", "-c", "a.c", "-c", "b.c", "-o", "a.o"]):
            with self.assertRaises(ValueError):
                checker.compile_command(
                    {"arguments": args}, Path("a.c"), Path("a.o"), []
                )

    def test_output_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                checker.prepare_output(root)
            output = root / "check"
            checker.prepare_output(output)
            checker.prepare_output(output)
            self.assertTrue((output / ".pebble-tuya-kernel-check").is_file())


if __name__ == "__main__":
    unittest.main()
