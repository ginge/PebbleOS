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
    def test_t5ai_flash_writes_are_limited_to_data_partitions(self):
        flash = (ROOT / "src/fw/drivers/flash/t5ai.c").read_text()
        for region in (
            "FLASH_REGION_FILESYSTEM_BEGIN",
            "FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN",
            "FLASH_REGION_CD_BEGIN",
            "FLASH_REGION_DEBUG_DB_BEGIN",
            "FLASH_REGION_SHARED_PRF_STORAGE_BEGIN",
        ):
            self.assertIn(region, flash)
        self.assertNotIn("FLASH_REGION_FIRMWARE_SLOT_0_BEGIN", flash)
        self.assertNotIn("FLASH_REGION_FIRMWARE_SLOT_1_BEGIN", flash)

    def test_t5ai_persistent_data_features_are_enabled(self):
        main = (ROOT / "src/fw/main.c").read_text()
        bluetooth_build = (ROOT / "src/fw/services/bluetooth/wscript_build").read_text()
        shared_prf = (
            ROOT / "src/fw/services/shared_prf_storage/shared_prf_storage.c"
        ).read_text()
        put_bytes = (ROOT / "src/fw/services/put_bytes/put_bytes.c").read_text()
        core_dump = (ROOT / "src/fw/kernel/core_dump.c").read_text()
        debug_build = (ROOT / "src/fw/debug/wscript_build").read_text()
        resources = (ROOT / "src/fw/resource/resource_storage_flash.c").read_text()

        self.assertIn("shared_prf_storage_init();", main)
        self.assertIn("bt_persistent_storage_init();", main)
        self.assertIn("put_bytes_init();", main)
        self.assertIn("bluetooth_persistent_storage_normal.c", bluetooth_build)
        self.assertNotIn("bluetooth_persistent_storage_t5ai.c", bluetooth_build)
        self.assertIn("flash_write_bytes(data, address, length);", shared_prf)
        t5ai_types = put_bytes.split("#ifdef CONFIG_SOC_T5AI", 1)[1].split("#endif", 1)[0]
        self.assertNotIn("ObjectSysResources", t5ai_types)
        self.assertNotIn("Persistent core dumps unavailable on T5AI", core_dump)
        self.assertEqual(debug_build.count("flash_logging_unavailable.c"), 1)
        self.assertIn("#ifndef CONFIG_SOC_T5AI", resources)

    def test_t5ai_shared_i2c_bus_uses_cst92xx_qualified_standard_mode(self):
        board = (ROOT / "src/fw/board/boards/board_t5ai.c").read_text()
        self.assertIn(".frequency = 100000u", board)
        self.assertNotIn(".frequency = 400000u", board)

    def test_t5ai_i2c_reads_use_the_bk7258_receive_fifo_levels(self):
        i2c = (ROOT / "src/fw/drivers/i2c/t5ai.c").read_text()
        self.assertIn("static uint32_t prv_read_interrupt_mode", i2c)
        self.assertIn("static uint32_t prv_read_fifo_count", i2c)
        self.assertIn("static uint32_t prv_write_fifo_space", i2c)
        self.assertIn("for (uint32_t i = 0; i < available; ++i)", i2c)
        self.assertIn("for (uint32_t i = 0; i < space; ++i)", i2c)
        self.assertIn("prv_read_interrupt_mode(transfer->size - transfer->idx)", i2c)

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
        display = (ROOT / "src/fw/drivers/display/t5ai/display_co5300.c").read_text()
        backlight = (ROOT / "src/fw/drivers/backlight/t5ai.c").read_text()
        touch = (ROOT / "src/fw/drivers/touch/cst92xx/cst92xx_t5ai.c").read_text()
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
        self.assertIn("T5AI_CP_LOG_RX_CHANNEL 0x4eu", dbgserial_input)
        self.assertIn("T5AI_CP_LOG_USER_INPUT_COMMAND 3u", dbgserial_input)
        self.assertIn("prv_log_mailbox_receive", dbgserial_input)
        self.assertIn(
            "t5ai_cp_mailbox_acknowledge(header, parameter1, parameter2, handled)",
            dbgserial_input,
        )
        self.assertIn(".parameter1 = parameter1", mailbox)
        self.assertIn(".parameter2 = parameter2", mailbox)
        self.assertIn("t5ai_cp_mailbox_process_pending();", main)
        self.assertIn("dbgserial_input_consume_mailbox_probe()", main)
        self.assertIn('dbgserial_putstr("PBLRX-OK: T5AI USB mailbox RX passed")', main)
        self.assertIn("DbgSerialMailboxProbe_Timebase", dbgserial_input)
        self.assertIn("DbgSerialMailboxProbe_Timebase", main)
        self.assertIn("DbgSerialMailboxProbe_Display", dbgserial_input)
        self.assertIn("DbgSerialMailboxProbe_Display", main)
        self.assertIn("DbgSerialMailboxProbe_Splash", dbgserial_input)
        self.assertIn("DbgSerialMailboxProbe_Splash", main)
        self.assertIn(
            "else if (memcmp(request, s_mailbox_display_probe_request",
            dbgserial_input,
        )
        self.assertIn("s_mailbox_splash_request", dbgserial_input)
        self.assertIn("boot_splash_start();", main)
        self.assertIn('dbgserial_putstr("PBLSPL done")', main)
        self.assertIn("const status_t boot_power_status", display)
        self.assertIn("prv_qspi_init() && prv_panel_init()", display)
        self.assertIn("prv_register(0x13u, NULL, 0) && prv_register(0x29u, NULL, 0)", display)
        panel_init = display.split("static bool prv_panel_init(void)", 1)[1].split(
            "static void prv_bitbang_write_pin", 1
        )[0]
        self.assertNotIn("prv_register(0x23u", panel_init)
        self.assertIn("prv_read_register(0x0au, &power_mode_raw)", display)
        self.assertIn("exact compiled Tuya ordering", display)
        self.assertIn(
            "GPIO_BASE[pin] = (GPIO_BASE[pin] & ~0x3fffu) | (1u << 3) | (1u << 6) | (3u << 8)",
            display,
        )
        self.assertIn("*prv_qspi(QSPI_CMD_C_CFG1) = 0x300u", display)
        self.assertNotIn("0x3u << ((length + 4u) * 2u)", display)
        brightness = display.index("{0x51, 0xff}")
        sleep_out = display.index("prv_register(0x11u")
        self.assertLess(brightness, sleep_out)
        self.assertIn(
            "*prv_qspi(QSPI_CONFIG) = QSPI_CS_HIGH_WAIT | QSPI_ENABLE | QSPI_IO23_ENABLE",
            display,
        )
        self.assertIn("#define QSPI_CS_HIGH_WAIT (8u << 24)", display)
        self.assertIn("SYSTEM_CPU1_INTERRUPT_ENABLE |= 1u << 20", display)
        self.assertNotIn("NVIC_EnableIRQ(QSPI0_IRQn)", display)
        self.assertIn("*prv_qspi(QSPI_GLB_CTRL) |= 1u", display)
        self.assertIn("(5u << 6)", display)
        self.assertIn("*prv_qspi(QSPI_RST_CFG) = 0u", display)
        self.assertIn('"PBLPWR clk=%08lx', display)
        self.assertIn('"PBLPIN p22-27=%03lx', display)
        self.assertIn('"PBLREG %02lx=%08lx', display)
        self.assertNotIn("prv_bitbang_panel_init", display)
        self.assertIn("void display_co5300_bitbang_probe(void)", display)
        self.assertIn("*prv_qspi(QSPI_CONFIG) &= ~QSPI_ENABLE", display)
        self.assertIn('prv_bitbang_byte(clock, data, 0x02u)', display)
        self.assertIn("GPIO_PuPd_NOPULL, data0_float", display)
        self.assertIn("GPIO_PuPd_UP, data0_up", display)
        self.assertIn("GPIO_PuPd_DOWN, data0_down", display)
        self.assertIn('"pull-down=%02x%02x%02x%02x/%02x%02x%02x%02x', display)
        self.assertIn('"PBLBIT done', display)
        self.assertIn("static bool prv_draw_diagnostic_pattern(void)", display)
        self.assertIn("const bool pattern_written = reinitialized", display)
        self.assertIn('"PBLDSP bootpm=%ld nowpm=%ld reinit=%u pattern=%u', display)
        self.assertIn("return 100u", backlight)
        self.assertIn("display_co5300_set_brightness(0xffu)", backlight)
        self.assertNotIn("* 255u", backlight)
        self.assertIn(
            "PBL_DISPLAY_WIDTH - 1 -\n"
            "                        prv_map_coordinate(native_x, PBL_DISPLAY_WIDTH)",
            touch,
        )
        self.assertIn(
            "PBL_DISPLAY_HEIGHT - 1 -\n"
            "                        prv_map_coordinate(native_y, PBL_DISPLAY_HEIGHT)",
            touch,
        )
        poll = touch[touch.index("static void prv_poll") : touch.index("static void prv_touch_task")]
        self.assertIn("s_consecutive_read_failures >= 3u", poll)
        self.assertIn("i2c_bitbang_recovery(I2C_CST92XX)", poll)
        self.assertIn("prv_reset(true);", poll)
        read_report_start = touch.index("static bool prv_read_report")
        read_report = touch[
            read_report_start : touch.index("static int16_t prv_map_coordinate", read_report_start)
        ]
        self.assertNotIn("system_task_watchdog_feed();", read_report)
        self.assertIn("CST92XX_REPORT_ACK", read_report)
        self.assertIn("sizeof(acknowledge), acknowledge", read_report)
        self.assertIn('xTaskCreate(prv_touch_task, "CST92xx"', touch)
        self.assertIn("CST92XX_TASK_PRIORITY (configMAX_PRIORITIES - 1)", touch)
        self.assertNotIn("system_task_add_callback(prv_poll", touch)
        touch_nav = (ROOT / "src/fw/applib/ui/recognizer/touch_nav.c").read_text()
        self.assertIn("state->counters.position_updates++", touch_nav)
        self.assertIn("state->counters.widget_started++", touch_nav)
        self.assertIn("s_mailbox_touch_nav_request", dbgserial_input)
        self.assertIn('"PBLTN ev=%u/%u/%u widget=%u/%u/%u/%u route=%u mgr=%u "', main)
        self.assertIn("touch_get_diagnostics(&service)", main)
        self.assertIn("event_loop_get_touch_diagnostics(&event_loop)", main)
        self.assertIn("++s_physical_samples", (ROOT / "src/fw/services/touch/touch.c").read_text())
        events = (ROOT / "src/fw/kernel/events.c").read_text()
        system_task = (ROOT / "src/fw/services/system_task/service.c").read_text()
        self.assertIn(
            "MAX_SYSTEM_EVENT_SET_EVENTS =\n"
            "      MAX_KERNEL_EVENTS + MAX_FROM_APP_EVENTS + MAX_FROM_WORKER_EVENTS",
            events,
        )
        self.assertIn(
            "xQueueCreateSet(MAX_SYSTEM_EVENT_SET_EVENTS)",
            events,
        )
        self.assertIn(
            "const UBaseType_t set_count = uxQueueMessagesWaiting(s_system_event_queue_set)",
            events,
        )
        self.assertIn("taskENTER_CRITICAL();", events)
        self.assertIn("taskEXIT_CRITICAL();", events)
        process_reset = events.split("void event_reset_from_process_queue", 1)[1].split(
            "QueueHandle_t event_kernel_to_kernel_event_queue", 1
        )[0]
        self.assertNotIn("xQueueReset(s_system_event_queue_set)", process_reset)
        event_take = events.split("bool event_take_timeout", 1)[1].split(
            "void **event_get_buffer", 1
        )[0]
        self.assertNotIn("#ifdef CONFIG_SOC_T5AI", event_take)
        self.assertNotIn("(void)timeout_ms", event_take)
        self.assertIn("#define SYSTEM_TASK_PRIORITY (configMAX_PRIORITIES - 1)", system_task)
        self.assertIn("#define SYSTEM_TASK_PRIORITY (tskIDLE_PRIORITY + 1)", system_task)
        self.assertIn("static volatile SystemTaskEventCallback s_current_cb", system_task)
        idle_watchdog = system_task.split(
            "static void system_task_idle_timer_callback", 1
        )[1].split("static void system_task_main", 1)[0]
        self.assertIn("if (s_current_cb == NULL)", idle_watchdog)
        self.assertNotIn("uxQueueMessagesWaiting", idle_watchdog)
        splash = (ROOT / "src/fw/services/boot_splash/service.c").read_text()
        self.assertIn("#if defined(CONFIG_SOC_T5AI)", splash)
        self.assertIn("static uint8_t s_t5ai_splash_row[PBL_DISPLAY_WIDTH]", splash)
        self.assertIn("display_update(prv_t5ai_splash_next_row", splash)
        self.assertIn("The logo is compiled into the AP image", splash)
        t5ai_splash = splash.split("#if defined(CONFIG_SOC_T5AI)", 1)[1].split(
            "#elif defined(CONFIG_PBLBOOT)", 1
        )[0]
        self.assertNotIn("kernel_malloc", t5ai_splash)
        self.assertIn(
            '"PBLTIM freertos=%lu rtc=%lu aon=%lu core=%lu reload=%lu "', main
        )
        self.assertIn('"stage=%lu appstage=%lu appinit=%u first=%u ', main)
        self.assertIn('"apptask=%u state=%u prio=%u stack=%u km=%u/%u bg=%u/%u "', main)
        self.assertIn('"nt=%u/%u/%u bth=%u/%u bti=%u/%u "', main)
        self.assertIn('"q=%lu/%lu/%lu/%lu/%lu take=%lu cur=%lu "', main)
        self.assertIn('"irq=%08lx/%08lx/%08lx"', main)
        self.assertIn("event_get_queue_diagnostics(&queue_diagnostics)", main)
        self.assertIn(
            "T5AI_BOOT_STAGE(26u)",
            (ROOT / "src/fw/kernel/event_loop.c").read_text(),
        )
        self.assertIn("g_t5ai_boot_stage = 6u", main)
        self.assertIn("volatile uint32_t g_t5ai_app_stage", main)
        self.assertIn("app_manager_is_initialized()", main)
        self.assertIn("app_manager_is_first_app_launched()", main)
        self.assertIn("cron_service_init();", main)
        self.assertIn("accel_manager_init();", main)
        self.assertIn("touch_init();", main)
        self.assertIn("persist_service_init();", main)
        self.assertIn("dls_init();", main)
        self.assertIn("app_order_storage_init();", main)
        event_loop = (ROOT / "src/fw/kernel/event_loop.c").read_text()
        self.assertIn("app_install_manager_init();", event_loop)
        launcher_menu = (
            ROOT / "src/fw/apps/system/launcher/default/menu_layer.c"
        ).read_text()
        self.assertIn("app_menu_data_source_draw_row(data_source", launcher_menu)
        self.assertIn("#ifndef CONFIG_SOC_T5AI\n  launcher_app_glance_service_init", launcher_menu)
        resource_impl = (ROOT / "src/fw/resource/resource_impl.def").read_text()
        t5ai_resource_impl = resource_impl.split("#if defined(CONFIG_SOC_T5AI)", 1)[1].split(
            "#elif", 1
        )[0]
        self.assertIn("RESOURCE_IMPL(g_builtin_impl)", t5ai_resource_impl)
        self.assertIn("RESOURCE_IMPL(g_system_bank_impl)", t5ai_resource_impl)
        builtin_storage = (ROOT / "src/fw/resource/resource_storage_builtin.c").read_text()
        self.assertNotIn(
            "defined(CONFIG_RECOVERY_FW) || defined(CONFIG_SOC_T5AI)", builtin_storage
        )
        flash_storage = (ROOT / "src/fw/resource/resource_storage_flash.c").read_text()
        self.assertIn("legacy_defective_checksum_memory(", flash_storage)
        self.assertIn("(const void *)(uintptr_t)(BANK.begin + offset)", flash_storage)
        self.assertIn("T5AI_BOOT_STAGE(7u)", event_loop)
        self.assertIn("T5AI_BOOT_STAGE(22u)", event_loop)
        self.assertIn(
            "normal blocking receive does not provide a scheduling window",
            event_loop,
        )
        self.assertIn("vTaskDelay(10u);", event_loop)
        self.assertIn(
            "#ifndef CONFIG_SOC_T5AI\n  // emulate a button press-and-release",
            event_loop,
        )
        shell_event_loop = (ROOT / "src/fw/shell/normal/shell_event_loop.c").read_text()
        self.assertIn("T5AI_SHELL_STAGE(171u)", shell_event_loop)
        self.assertIn("T5AI_SHELL_STAGE(180u)", shell_event_loop)
        self.assertIn(
            "#else\n      battery_ui_handle_state_change_event",
            shell_event_loop,
        )
        shell_prefs = (ROOT / "src/fw/shell/normal/prefs.c").read_text()
        self.assertNotIn("DVT3 deliberately does not initialize writable PFS", shell_prefs)
        self.assertIn("#ifndef CONFIG_SOC_T5AI\n  prv_maybe_run_settings", shell_event_loop)
        self.assertIn(
            "#ifndef CONFIG_SOC_T5AI\n  bool factory_reset_or_first_use",
            shell_event_loop,
        )
        services = (ROOT / "src/fw/services/services.c").read_text()
        services_common = (ROOT / "src/fw/services/services_common/service.c").read_text()
        process_manager = (ROOT / "src/fw/process_management/process_manager.c").read_text()
        app_install_manager = (
            ROOT / "src/fw/process_management/app_install_manager.c"
        ).read_text()
        app_order_storage = (
            ROOT / "src/fw/services/process_management/app_order_storage.c"
        ).read_text()
        app_manager = (ROOT / "src/fw/process_management/app_manager.c").read_text()
        launcher = (
            ROOT / "src/fw/apps/system/launcher/default/launcher.c"
        ).read_text()
        app_state = (ROOT / "src/fw/process_state/app_state/app_state.c").read_text()
        self.assertIn(
            "#if !defined(CONFIG_RECOVERY_FW) && !defined(CONFIG_SOC_T5AI)\n"
            "  services_normal_set_runlevel(runlevel);",
            services,
        )
        self.assertIn("#ifndef CONFIG_SOC_T5AI\n  {\n    .set_enable_fn = accel_manager_enable", services_common)
        self.assertIn("#ifndef CONFIG_SOC_T5AI\n  {\n    .set_enable_fn = light_allow", services_common)
        self.assertIn("persist_service_client_open", process_manager)
        self.assertIn("persist_service_client_close", process_manager)
        self.assertNotIn("if (dls_initialized())", process_manager)
        self.assertIn(
            "#ifndef CONFIG_SOC_T5AI\n  // DVT3 currently has no initialized AppDB",
            app_install_manager,
        )
        self.assertNotIn("#ifdef CONFIG_SOC_T5AI", app_order_storage)
        self.assertIn("T5AI_APP_STAGE(201u)", app_manager)
        self.assertIn("T5AI_APP_STAGE(205u)", app_manager)
        self.assertIn("T5AI_LAUNCHER_STAGE(211u)", launcher)
        self.assertIn("T5AI_LAUNCHER_STAGE(224u)", launcher)
        self.assertIn("T5AI_APP_STATE_STAGE(301u)", app_state)
        self.assertIn("T5AI_APP_STATE_STAGE(323u)", app_state)
        self.assertIn("T5AI_APP_STATE_STAGE(324u)", app_state)
        self.assertIn("T5AI_APP_STATE_STAGE(326u)", app_state)
        self.assertIn("t5ai_rtc_get_raw_ticks()", main)
        self.assertIn("(unsigned long)SystemCoreClock", main)
        self.assertIn("(unsigned long)SysTick->LOAD", main)
        self.assertNotIn("s_character_callback", dbgserial_input.split("#elif", 1)[0])
        self.assertIn("dbgserial_putchar_lazy(*(msg++))", serial_console)
        self.assertIn("T5AI_CP_CONTROL_CHANNEL 0x10u", mailbox)
        self.assertIn("T5AI_CP_POWER_UP_COMMAND 1u", mailbox)
        self.assertIn("T5AI_CP_HEARTBEAT_COMMAND 2u", mailbox)
        self.assertIn("((uint32_t)s_control_sequence << 16)", mailbox)
        self.assertIn("message->parameter1 == 0u", mailbox)
        self.assertIn("T5AI_CP_PM_TX_CHANNEL 0x12u", mailbox)
        self.assertIn("T5AI_CP_PM_RX_CHANNEL 0x42u", mailbox)
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
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        compositor = (
            ROOT / "src/fw/services/compositor/compositor.c"
        ).read_text()
        self.assertNotIn("0x2809f700u", initialization)
        self.assertIn("T5AI_CP_PM_PSRAM_POWER_COMMAND 7u", mailbox)
        self.assertIn("T5AI_CP_PM_PSRAM_APP_MODULE 9u", mailbox)
        vote = compositor.index("t5ai_cp_mailbox_enable_psram()")
        validate = compositor.index("prv_validate_t5ai_framebuffer_ram()", vote)
        self.assertLess(vote, validate)

    def test_t5ai_core_clock_is_an_explicit_systick_contract(self):
        kconfig = (ROOT / "soc/Kconfig").read_text()
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        port = (ROOT / "soc/t5ai/freertos.c").read_text()
        main = (ROOT / "src/fw/main.c").read_text()
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        self.assertIn("config T5AI_CORE_CLOCK_HZ", kconfig)
        self.assertIn("default 120000000", kconfig)
        self.assertIn("config T5AI_SYSTICK_CLOCK_HZ", kconfig)
        self.assertIn("application CPU SysTick counter advances at 60 MHz", kconfig)
        self.assertIn(
            "SystemCoreClock = CONFIG_T5AI_CORE_CLOCK_HZ", initialization
        )
        self.assertIn(
            "CONFIG_T5AI_SYSTICK_CLOCK_HZ % RTC_TICKS_HZ == 0u", port
        )
        self.assertIn("T5AI_SYSTICK_RELOAD <= SysTick_LOAD_RELOAD_Msk", port)
        self.assertIn("t5ai_freertos_apply_systick_clock", port)
        self.assertIn("SysTick->LOAD = T5AI_SYSTICK_RELOAD", port)
        self.assertIn("return false;", port)
        self.assertIn("t5ai_freertos_apply_systick_clock();", main)
        self.assertIn("t5ai_cp_mailbox_enable_rx_interrupt();", main)
        self.assertIn("void MAILBOX_IRQHandler(void)", mailbox)
        self.assertIn("system_task_add_callback_from_isr", mailbox)
        self.assertNotIn("new_timer_add_work_callback_from_isr", mailbox)
        self.assertIn("T5AI_MBOX_CH1_CTRL |= T5AI_MBOX_INTERRUPT_ENABLE", mailbox)
        self.assertIn("T5AI_MAILBOX_SYSTEM_INTERRUPT_ENABLE", mailbox)
        self.assertIn("T5AI_CP_PM_CPU_FREQUENCY_COMMAND 4u", mailbox)
        self.assertIn("T5AI_CP_PM_CPU1_MODULE 38u", mailbox)
        self.assertIn("T5AI_CP_PM_CPU_FREQUENCY_120MHZ 3u", mailbox)
        self.assertIn("t5ai_cp_mailbox_vote_cpu1_120mhz", mailbox)
        vote = main.index("t5ai_cp_mailbox_vote_cpu1_120mhz()")
        self.assertLess(main.index("init_drivers();"), vote)
        self.assertLess(vote, main.index("display_init();", vote))
        self.assertIn('"PBLCLK vote=%ld mode1=%08lx', main)
        self.assertIn('"PBLCLK apply mode1=%08lx', main)
        self.assertIn(
            "T5AI_CP_LOG_RX_CHANNEL 0x4eu",
            (ROOT / "src/fw/console/dbgserial_input.c").read_text(),
        )

    def test_t5ai_debug_uart_keeps_the_board_runtime_baud_rate(self):
        console = (ROOT / "src/fw/console/dbgserial.c").read_text()
        driver = (ROOT / "src/fw/drivers/uart/t5ai.c").read_text()
        self.assertIn(
            "#elif defined(CONFIG_SOC_T5AI)\n"
            "#define DEFAULT_SERIAL_BAUD_RATE 460800",
            console,
        )
        self.assertIn("uart->global_ctrl = (1u << 0) | (1u << 1);", driver)
        self.assertIn(
            "T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE",
            driver,
        )
        self.assertIn(
            "T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE",
            driver,
        )
        self.assertIn("UART_INT_RX_FINISH", driver)

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
        app_manager = (ROOT / "src/fw/process_management/app_manager.c").read_text()
        self.assertIn("#define SYSTEM_TASK_PRIORITY (configMAX_PRIORITIES - 1)", service)
        self.assertIn("must drain producers such as the once-per-second advertising cycle", service)
        self.assertIn("#ifdef CONFIG_SOC_T5AI", app_manager)
        self.assertIn("const UBaseType_t app_task_priority = tskIDLE_PRIORITY + 1", app_manager)
        self.assertIn(".uxPriority = app_task_priority | portPRIVILEGE_BIT", app_manager)
        task = service.split("static void system_task_main", 1)[1]
        task = task.split("void system_task_init", 1)[0]
        self.assertIn("#ifdef CONFIG_SOC_T5AI", task)
        self.assertIn("milliseconds_to_ticks(1000)", task)
        self.assertIn("xSemaphoreTake(s_system_task_wake, wait_ticks)", task)
        self.assertIn("xQueueReceive(s_system_task_queue, &event, 0)", task)
        self.assertIn("xQueueReceive(s_from_app_system_task_queue, &event, 0)", task)
        self.assertIn("system_task_watchdog_feed();", task)
        self.assertIn("xSemaphoreGiveFromISR(s_system_task_wake", service)
        self.assertIn("xSemaphoreGive(s_system_task_wake)", service)

    def test_t5ai_initializes_native_button_debouncing(self):
        main = (ROOT / "src/fw/main.c").read_text()
        exti = (ROOT / "src/fw/drivers/exti/t5ai.c").read_text()
        dbgserial_input = (ROOT / "src/fw/console/dbgserial_input.c").read_text()
        button_init = main.split(
            '// entering the kernel event queue.\n', 1
        )[1].split("#ifndef CONFIG_SOC_T5AI", 1)[0]
        self.assertIn("debounced_button_init();", button_init)
        button_debounce = (
            ROOT / "src/fw/drivers/t5ai/debounced_button.c"
        ).read_text()
        self.assertIn("system_task_add_callback_from_isr", button_debounce)
        self.assertIn("new_timer_stop(s_debounce_timer)", button_debounce)
        self.assertNotIn("new_timer_add_work_callback_from_isr", button_debounce)
        self.assertIn("T5AI_SYSTEM_CPU1_INT_32_63_ENABLE", exti)
        self.assertIn("T5AI_GPIO_INTERRUPT_CLEAR (1u << 13)", exti)
        self.assertIn("*prv_pin_register(pin) |= T5AI_GPIO_INTERRUPT_CLEAR", exti)
        self.assertNotIn("DbgSerialMailboxProbe_Buttons", dbgserial_input)

    def test_t5ai_i2c_and_external_rtc(self):
        rtc = (ROOT / "src/fw/drivers/rtc/t5ai.c").read_text()
        i2c = (ROOT / "src/fw/drivers/i2c/t5ai.c").read_text()
        self.assertIn("T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= 1u << 6", i2c)
        self.assertNotIn("T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE", i2c)
        enable = i2c.split("void i2c_hal_enable", 1)[1].split(
            "void i2c_hal_disable", 1
        )[0]
        self.assertIn("delay_us(5u);", enable)
        start = i2c.split("void i2c_hal_start_transfer", 1)[1].split(
            "void i2c_irq_handler", 1
        )[0]
        self.assertIn("delay_us(5u);", start)
        disable = i2c.split("void i2c_hal_disable", 1)[1].split(
            "bool i2c_hal_is_busy", 1
        )[0]
        self.assertNotIn("i2c_hal_abort_transfer", disable)
        self.assertIn("~T5AI_I2C_CONFIG_ENABLE", disable)
        self.assertIn("if (!prv_external_rtc_read_raw(&control, registers))", rtc)
        self.assertIn("prv_set_retained_time(external_time);", rtc)
        self.assertIn("prv_external_rtc_write(fallback_time)", rtc)
        self.assertIn("T5AI_RTC_MIN_VALID_TIME", rtc)
        self.assertIn("if (base_ticks > ticks", rtc)

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
