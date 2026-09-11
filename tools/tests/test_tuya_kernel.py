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

    def test_t5ai_announces_ap_startup_to_the_communication_processor(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        startup = (ROOT / "src/fw/startup/startup_cortex_m.c").read_text()
        main = (ROOT / "src/fw/main.c").read_text()
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        port = (ROOT / "soc/t5ai/freertos.c").read_text()
        marker = startup.index("uart0[7] = 'A'")
        initialize_soc = startup.index("SystemInit()")
        enable_cache = startup.index("icache_enable()")
        announce = initialization.index("t5ai_cp_mailbox_announce_power_up()")
        enable_irq = initialization.index("__enable_irq()")
        self.assertLess(marker, initialize_soc)
        self.assertLess(initialize_soc, enable_cache)
        self.assertLess(announce, enable_irq)
        self.assertIn("< 3200u", initialization)
        self.assertIn("T5AI_CP_LOG_CHANNEL 0x1eu", mailbox)
        self.assertIn("T5AI_CP_LOG_ASSERT_OUT_COMMAND 4u", mailbox)
        self.assertIn("s_log_buffer[0] = 1u", mailbox)
        self.assertIn("dcache_invalidate(s_log_buffer, 1u)", mailbox)
        dbgserial = (ROOT / "src/fw/console/dbgserial.c").read_text()
        dbgserial_input = (ROOT / "src/fw/console/dbgserial_input.c").read_text()
        serial_console = (ROOT / "src/fw/console/serial_console.c").read_text()
        self.assertIn("t5ai_cp_mailbox_log(s_t5ai_buffer", dbgserial)
        self.assertIn("!defined(CONFIG_SOC_T5AI)", dbgserial_input)
        self.assertIn("dbgserial_putchar_lazy(*(msg++))", serial_console)
        self.assertIn("T5AI_CP_CONTROL_CHANNEL 0x10u", mailbox)
        self.assertIn("T5AI_CP_POWER_UP_COMMAND 1u", mailbox)
        self.assertIn("T5AI_CP_HEARTBEAT_COMMAND 2u", mailbox)
        self.assertIn("((uint32_t)s_control_sequence << 16)", mailbox)
        self.assertIn("message->parameter1 == 0u", mailbox)
        self.assertNotIn(
            "message->header >> 16u) == s_control_sequence", mailbox
        )
        self.assertIn("void vApplicationTickHook(void)", port)
        self.assertNotIn("t5ai_cp_mailbox_heartbeat();", port)
        self.assertIn("t5ai_cp_mailbox_heartbeat();", main)
        self.assertIn("regular_timer_add_seconds_callback(&cp_heartbeat_timer)", main)
        heartbeat = mailbox.split("void t5ai_cp_mailbox_heartbeat(void)", 1)[1]
        heartbeat = heartbeat.split("static status_t prv_pm_request", 1)[0]
        self.assertIn("mutex_lock_with_timeout(mutex, 0u)", heartbeat)
        self.assertIn("sizeof(s_heartbeat_parameter), false", heartbeat)
        self.assertNotIn("prv_control_request", heartbeat)
        self.assertIn(
            "pdMS_TO_TICKS(TASK_WATCHDOG_FEED_PERIOD_MS)", port
        )
        self.assertIn("task_watchdog_feed();", port)
        config = (ROOT / "third_party/freertos/FreeRTOSConfig.h").read_text()
        self.assertIn(
            "#if defined(CONFIG_SOC_T5AI)\n#define configUSE_TICK_HOOK      1",
            config,
        )

    def test_t5ai_holds_psram_power_for_the_compositor_framebuffer(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        hold_psram = initialization.index(
            "T5AI_AP_PSRAM_USE_COUNT = T5AI_AP_PSRAM_PERMANENT_USE"
        )
        announce = initialization.index("t5ai_cp_mailbox_announce_power_up()")
        self.assertLess(hold_psram, announce)
        self.assertIn("T5AI_SHARED_POWER_STATE_BASE 0x2809f700u", initialization)
        self.assertIn("T5AI_AP_PSRAM_PERMANENT_USE 100u", initialization)

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

    def test_t5ai_debug_uart_keeps_the_board_runtime_baud_rate(self):
        console = (ROOT / "src/fw/console/dbgserial.c").read_text()
        self.assertIn(
            "#elif defined(CONFIG_SOC_T5AI)\n"
            "#define DEFAULT_SERIAL_BAUD_RATE 460800",
            console,
        )

    def test_t5ai_enables_global_interrupts_at_first_task_entry(self):
        main = (ROOT / "src/fw/main.c").read_text()
        task_entry = main.index("static void main_task(")
        enable_irq = main.index("__enable_irq()", task_entry)
        initialize = main.index("prv_main_task_init();", task_entry)
        self.assertLess(task_entry, enable_irq)
        self.assertLess(enable_irq, initialize)

    def test_t5ai_leaves_cp_owned_watchdog_disabled(self):
        board = (ROOT / "src/fw/board/boards/board_t5ai.c").read_text()
        early_init = board.split("void board_early_init(void)", 1)[1]
        early_init = early_init.split("void board_init(void)", 1)[0]
        self.assertIn("watchdog_init();", early_init)
        self.assertNotIn("watchdog_start();", early_init)

    def test_t5ai_kernelbg_proves_liveness_from_its_idle_wait(self):
        service = (ROOT / "src/fw/services/system_task/service.c").read_text()
        self.assertIn(
            "#ifdef CONFIG_SOC_T5AI\n"
            "// Until T5AI enables the full service stack",
            service,
        )
        self.assertIn("#define SYSTEM_TASK_PRIORITY (tskIDLE_PRIORITY + 3)", service)
        task = service.split("static void system_task_main", 1)[1]
        task = task.split("void system_task_init", 1)[0]
        self.assertIn("#ifdef CONFIG_SOC_T5AI", task)
        self.assertIn("milliseconds_to_ticks(1000)", task)
        self.assertIn("if (activated_queue == NULL)", task)
        self.assertIn("system_task_watchdog_feed();", task)

    def test_t5ai_initializes_native_button_debouncing(self):
        main = (ROOT / "src/fw/main.c").read_text()
        button_init = main.split(
            '// entering the kernel event queue.\n', 1
        )[1].split("#ifndef CONFIG_SOC_T5AI", 1)[0]
        self.assertIn("debounced_button_init();", button_init)

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
