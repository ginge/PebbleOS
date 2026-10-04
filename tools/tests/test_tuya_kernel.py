# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaKernelCheckTest(unittest.TestCase):
    def test_t5ai_safe_reset_requests_cpu0_whole_chip_reboot(self):
        reset = (ROOT / "src/fw/kernel/reset.c").read_text()
        hci_ipc = (
            ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c"
        ).read_text()
        hci_header = (ROOT / "include/pbl/soc/t5ai/hci_ipc.h").read_text()
        self.assertIn("#ifdef CONFIG_SOC_T5AI", reset)
        self.assertIn("t5ai_hci_ipc_request_system_reboot()", reset)
        self.assertIn("T5AI_SYSTEM_TYPE 1u", hci_ipc)
        self.assertIn("T5AI_SYSTEM_REBOOT 0u", hci_ipc)
        self.assertIn("s_system_reboot_request", hci_ipc)
        self.assertIn("prv_prepare_outbound(&s_system_reboot_request", hci_ipc)
        self.assertIn("t5ai_hci_ipc_request_system_reboot", hci_header)

    def test_t5ai_hardfault_retains_pc_when_lr_is_exception_return(self):
        handler = (ROOT / "src/fw/hardfault_handler.c").read_text()
        reboot_log = (ROOT / "src/fw/debug/debug_reboot_reason.c").read_text()
        self.assertIn("#ifdef CONFIG_SOC_T5AI", handler)
        self.assertIn("(stacked_lr & 0xfffffff0u) == 0xfffffff0u", handler)
        self.assertIn("hardfault_args[6]", handler)
        self.assertIn("reason.data16 = (uint16_t)SCB->CFSR", handler)
        self.assertIn("reason.hard_fault.pc = hardfault_args[6]", handler)
        self.assertIn("reason.hard_fault.lr = hardfault_args[5]", handler)
        self.assertIn("T5AI HardFault CFSR[15:0]", reboot_log)

    def test_t5ai_cacheable_mpu_regions_are_non_shareable(self):
        mpu = (ROOT / "src/fw/drivers/mpu/mpu_armv8m.c").read_text()
        self.assertIn("#ifdef CONFIG_SOC_T5AI", mpu)
        self.assertIn("region->cache_policy != MpuCachePolicy_NotCacheable", mpu)
        self.assertIn("shareability = ARM_MPU_SH_NON", mpu)

    def test_t5ai_reclaims_only_measured_gabbro_runtime_headroom(self):
        defaults = (ROOT / "soc/t5ai/Kconfig.defconfig").read_text()
        self.assertIn("config APP_RAM_GABBRO_RUNTIME_SIZE", defaults)
        self.assertIn("default 77824", defaults)
        self.assertIn("measured\n# AppState is 73,936 bytes", defaults)

    def test_t5ai_flash_writes_are_limited_to_data_partitions(self):
        flash = (ROOT / "src/fw/drivers/flash/t5ai.c").read_text()
        for region in (
            "FLASH_REGION_FILESYSTEM_BEGIN",
            "FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN",
            "FLASH_REGION_SHARED_PRF_STORAGE_BEGIN",
        ):
            self.assertIn(region, flash)
        for region in (
            "FLASH_REGION_CD_BEGIN",
            "FLASH_REGION_DEBUG_DB_BEGIN",
        ):
            self.assertNotIn(region, flash)
        self.assertNotIn("FLASH_REGION_FIRMWARE_SLOT_0_BEGIN", flash)
        self.assertNotIn("FLASH_REGION_FIRMWARE_SLOT_1_BEGIN", flash)

    def test_t5ai_bluetooth_persistence_uses_bounded_shared_prf_storage(self):
        main = (ROOT / "src/fw/main.c").read_text()
        bluetooth_build = (ROOT / "src/fw/services/bluetooth/wscript_build").read_text()
        flash_regions = (ROOT / "src/fw/flash_region/flash_region_t5ai.h").read_text()
        shared_prf = (
            ROOT / "src/fw/services/shared_prf_storage/shared_prf_storage.c"
        ).read_text()
        put_bytes = (ROOT / "src/fw/services/put_bytes/put_bytes.c").read_text()
        core_dump = (ROOT / "src/fw/kernel/core_dump.c").read_text()
        debug_build = (ROOT / "src/fw/debug/wscript_build").read_text()
        resources = (ROOT / "src/fw/resource/resource_storage_flash.c").read_text()

        self.assertIn("shared_prf_storage_init();", main)
        self.assertIn("bt_persistent_storage_init();", main)
        self.assertIn("comm_default_kernel_sender_init();", main)
        self.assertIn("put_bytes_init();", main)
        self.assertIn("not bld.env.CONFIG_SOC_T5AI", bluetooth_build)
        self.assertIn("bluetooth_persistent_storage_normal.c", bluetooth_build)
        self.assertIn("bluetooth_persistent_storage_prf.c", bluetooth_build)
        self.assertNotIn("bluetooth_persistent_storage_t5ai.c", bluetooth_build)
        self.assertFalse(
            (
                ROOT
                / "src/fw/services/bluetooth/bluetooth_persistent_storage_t5ai.c"
            ).exists()
        )
        nimble_build = (ROOT / "src/bluetooth-fw/nimble/wscript_build").read_text()
        self.assertIn("if bld.env.CONFIG_SOC_T5AI:", nimble_build)
        self.assertIn("excludes.append('gatt_client_discovery.c')", nimble_build)
        self.assertIn("excludes.append('gatt_client_discovery_t5ai.c')", nimble_build)
        self.assertIn("excludes.append('responsiveness.c')", nimble_build)
        self.assertIn("excludes.append('responsiveness_t5ai.c')", nimble_build)
        responsiveness = (
            ROOT / "src/bluetooth-fw/nimble/responsiveness_t5ai.c"
        ).read_text()
        self.assertIn("if (req->slave_latency_events != 0u)", responsiveness)
        self.assertIn("skipped latency=%u update", responsiveness)
        self.assertIn("T5AI_ACTIVE_SUPERVISION_TIMEOUT_10MS 2000u", responsiveness)
        self.assertIn("effective_req.supervision_timeout_10ms", responsiveness)
        self.assertIn("desc.role == BLE_GAP_ROLE_SLAVE", responsiveness)
        self.assertIn("ble_l2cap_sig_update", responsiveness)
        self.assertIn("ble_gap_update_params", responsiveness)
        hci_ipc = (ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c").read_text()
        hci_exchange = hci_ipc.split("static status_t prv_exchange(", 1)[1].split(
            "static status_t prv_locked_exchange(", 1
        )[0]
        self.assertIn("dcache_flush(payload, length);", hci_exchange)
        self.assertIn("request->message", hci_exchange)
        self.assertIn("s_sync_mutex", hci_ipc)
        self.assertIn("s_async_mutex", hci_ipc)
        self.assertIn("(void)t5ai_mipc_frame_valid(frame, data)", hci_ipc)
        self.assertIn("stable transaction-generation token", hci_ipc)
        self.assertIn("prv_mark_faulted()", hci_ipc)
        self.assertIn("send_busy_retries", hci_ipc)
        self.assertIn("exchange timed out", hci_ipc)
        self.assertIn("pending_queue_high_water", hci_ipc)
        self.assertIn("static void prv_worker", hci_ipc)
        self.assertIn('section(".t5ai_board_cache")', hci_ipc)
        self.assertIn("xTaskGenericCreate(prv_worker", hci_ipc)
        self.assertIn("xSemaphoreGive(s_exchange_ready)", hci_ipc)
        self.assertIn("stop left CP-owned descriptors quarantined", hci_ipc)
        self.assertIn("prv_all_outbound_retired()", hci_ipc)
        self.assertIn("t5ai_cp_mailbox_shared_range_valid", hci_ipc)
        self.assertNotIn("t5ai_cp_mailbox_lock()", hci_ipc)
        hci_start = hci_ipc.split("int t5ai_hci_ipc_start(", 1)[1].split(
            "int t5ai_hci_ipc_stop(", 1
        )[0]
        self.assertIn("prv_locked_exchange(T5AI_HCI_RESET, NULL, 0)", hci_start)
        self.assertNotIn("prv_locked_exchange(T5AI_HCI_INIT, NULL, 0)", hci_start)
        self.assertLess(
            hci_start.index("prv_locked_exchange(T5AI_HCI_RESET, NULL, 0)"),
            hci_start.index("prv_locked_exchange(T5AI_HCI_REGISTER_CALLBACKS, NULL, 0)"),
        )
        hci_route = hci_ipc.split("static void prv_route_frame(", 1)[1].split(
            "static status_t prv_decode_remote(", 1
        )[0]
        self.assertIn("t5ai_cp_mailbox_acknowledge_frame(frame, true)", hci_route)
        self.assertIn("logical_channel != T5AI_MIPC_RX_LOGICAL_CHANNEL", hci_route)
        self.assertIn("prv_queue_pending_frame(&pending)", hci_route)
        self.assertNotIn("prv_handle_command", hci_route)
        hci_decode = hci_ipc.split("static status_t prv_decode_remote(", 1)[1].split(
            "static status_t prv_respond_to_event(", 1
        )[0]
        self.assertNotIn("frame->header >> 24", hci_decode)
        flash_ipc = (ROOT / "src/fw/drivers/flash/t5ai_flash_ipc.c").read_text()
        self.assertIn("T5AIMbIpcSocket *s_flash_socket", flash_ipc)
        self.assertIn("t5ai_mb_ipc_discard(s_flash_socket)", flash_ipc)
        self.assertIn("prv_flash_transaction(FLASH_READ_DONE)", flash_ipc)
        self.assertIn("t5ai_mb_ipc_poison(s_flash_socket)", flash_ipc)
        self.assertIn("#define SARADC_IPC_TIMEOUT_MS 2000u", flash_ipc)
        self.assertIn("t5ai_mb_ipc_poison(s_saradc_socket)", flash_ipc)
        self.assertIn("s_saradc_available = false", flash_ipc)
        self.assertNotIn("t5ai_cp_mailbox_lock()", flash_ipc)
        mb_ipc = (ROOT / "src/fw/drivers/t5ai/mb_ipc.c").read_text()
        self.assertNotIn("timeout_ms < MB_IPC_PHYSICAL_TIMEOUT_MS", mb_ipc)
        self.assertIn("socket->poisoned = true", mb_ipc)
        mailbox = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()
        self.assertIn("static TxChannel s_tx[T5AI_MAILBOX_CHANNEL_COUNT]", mailbox)
        self.assertNotIn('section(".t5ai_board_cache")', mailbox)
        self.assertIn("xTaskCreate(prv_mailbox_task", mailbox)
        self.assertIn("before the CP powers PSRAM", mailbox)
        self.assertIn("#define MAILBOX_TASK_STACK_WORDS 512u", mailbox)
        put_bytes = (ROOT / "src/fw/services/put_bytes/put_bytes.c").read_text()
        put_bytes_buffers = (
            ROOT / "src/fw/drivers/t5ai/put_bytes_job_buffer.c"
        ).read_text()
        self.assertIn("put_bytes_job_buffer_alloc(i, PUT_BYTES_PP_BUFFER_SIZE)", put_bytes)
        self.assertIn('section(".t5ai_board_cache")', put_bytes_buffers)
        self.assertIn("T5AI_PUT_BYTES_JOB_COUNT 3u", put_bytes_buffers)
        self.assertIn("if (prv_any_active())", mailbox)
        self.assertIn("sequence != tx->sequence", mailbox)
        self.assertIn("tx->active_start", mailbox)
        self.assertIn(
            "MailboxOwnedSlot s_ack_slots[T5AI_MAILBOX_CHANNEL_COUNT]", mailbox
        )
        self.assertIn("s_ack_slots[channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK]", mailbox)
        self.assertIn("configMAX_PRIORITIES - 1u", mailbox)
        hci_transport = (
            ROOT / "third_party/nimble/transport/hci_t5ai.c"
        ).read_text()
        self.assertNotIn("s_failed", hci_transport)
        self.assertIn("T5AI HCI event delivery failed", hci_transport)
        self.assertIn("T5AI HCI ACL delivery failed", hci_transport)
        self.assertIn("T5AI HCI ACL transmit deferred", hci_transport)
        self.assertIn("T5AI HCI disconnect snapshot", hci_transport)
        self.assertIn("T5AI HCI pressure", hci_transport)
        self.assertIn("s_active_connections", hci_transport)
        self.assertIn(
            "Dropping stale T5AI HCI disconnect from prior AP generation",
            hci_transport,
        )
        self.assertIn("data[T5AI_HCI_EVENT_CODE_OFFSET] == BLE_HCI_EVCODE_LE_META", hci_transport)
        self.assertIn("ble_hs_sched_reset(BLE_HS_ECONTROLLER)", hci_transport)
        self.assertIn("s_reset_scheduled", hci_transport)
        self.assertIn("prv_schedule_reset();", hci_transport)
        self.assertIn("t5ai_hci_ipc_faulted()", hci_transport)
        self.assertIn("xQueueSend(s_acl_tx_queue, &acl, 0)", hci_transport)
        self.assertNotIn("prv_trace_ppog", hci_transport)
        self.assertNotIn("T5AI PP RX start", hci_transport)
        self.assertNotIn("T5AI BlobDB", hci_transport)
        self.assertNotIn("LOG_DOMAIN_BT_STACK", hci_transport)
        self.assertIn("PBL_LOG_D_DBG(LOG_DOMAIN_COMM", hci_transport)
        self.assertIn("T5AI_HCI_ACL_TX_QUEUE_CAPACITY", hci_transport)
        self.assertIn("prv_send_queued_acl", hci_transport)
        self.assertNotIn("T5AI_HCI_ACL_SEND_ATTEMPTS", hci_transport)
        self.assertIn("int result = BLE_ERR_MEM_CAPACITY;", hci_transport)
        self.assertIn("status=%d len=%u", hci_transport)
        connect_params = (
            ROOT / "src/fw/comm/ble/gap_le_connect_params_t5ai.c"
        ).read_text()
        self.assertIn("T5AI_FAST_INTERVAL_1_25MS 12u", connect_params)
        self.assertNotIn("T5AI_IDLE_INTERVAL_1_25MS", connect_params)
        self.assertNotIn("bt_driver_le_connection_parameter_update", connect_params)
        self.assertIn("conn_mgr_handle_desired_state_granted(connection, desired_state)", connect_params)
        reconnect = (
            ROOT / "src/fw/comm/ble/gap_le_slave_reconnect.c"
        ).read_text()
        self.assertIn("The BK7258 controller can stop responding", reconnect)
        self.assertIn("#ifndef CONFIG_SOC_T5AI", reconnect)
        reversed_ppog = (
            ROOT / "src/bluetooth-fw/nimble/ppog_reversed_service.c"
        ).read_text()
        ppogatt = (
            ROOT / "src/fw/comm/ble/kernel_le_client/ppogatt/ppogatt.c"
        ).read_text()
        self.assertIn("kernel_malloc(pkt_len)", reversed_ppog)
        self.assertIn("bt_driver_cb_ppog_reversed_data_written", reversed_ppog)
        self.assertNotIn("t5ai_ppog_heap_diagnostics", reversed_ppog)
        self.assertNotIn("CONFIG_SOC_T5AI", ppogatt)
        self.assertIn("uint8_t *buf;  // owned, freed by the KernelMain cb.", ppogatt)
        desired_rx_window = ppogatt.split(
            "const uint8_t desired_rx_window", 1
        )[1].split("client->out.rx_window_size", 1)[0]
        self.assertNotIn("CONFIG_SOC_T5AI", desired_rx_window)
        self.assertIn(
            "cb(&pairing_info.identity, &pairing_info.irk, device_name, &bonding, context);",
            (ROOT / "src/fw/services/bluetooth/bluetooth_persistent_storage_prf.c").read_text(),
        )
        self.assertIn("flash_write_bytes(data, address, length);", shared_prf)
        write_helper = shared_prf.split("static void prv_flash_write(", 1)[1].split("}", 1)[0]
        self.assertNotIn("CONFIG_SOC_T5AI", write_helper)
        self.assertIn(
            "FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x024ea000", flash_regions
        )
        self.assertIn(
            "FLASH_REGION_SHARED_PRF_STORAGE_END 0x024fa000", flash_regions
        )
        t5ai_types = put_bytes.split("#ifdef CONFIG_SOC_T5AI", 1)[1].split("#endif", 1)[0]
        self.assertIn("ObjectSysResources", t5ai_types)
        self.assertIn("Persistent core dumps unavailable on T5AI", core_dump)
        self.assertEqual(debug_build.count("flash_logging_unavailable.c"), 1)
        self.assertIn("#ifndef CONFIG_SOC_T5AI", resources)

    def test_t5ai_shared_i2c_bus_uses_cst92xx_qualified_standard_mode(self):
        board = (ROOT / "src/fw/board/boards/board_unhygienix.c").read_text()
        self.assertIn(".frequency = 100000u", board)
        self.assertNotIn(".frequency = 400000u", board)


    def test_t5ai_i2c_uses_stable_single_byte_interrupt_mode(self):
        i2c = (ROOT / "src/fw/drivers/i2c/t5ai.c").read_text()
        self.assertNotIn("static uint32_t prv_read_fifo_count", i2c)
        self.assertNotIn("static uint32_t prv_write_fifo_space", i2c)
        self.assertIn("transfer->data[transfer->idx++] = (uint8_t)regs->data;", i2c)
        self.assertIn("regs->data = transfer->data[transfer->idx++];", i2c)
        self.assertIn(
            "T5AI_I2C_STATUS_ACK | T5AI_I2C_STATUS_INT_MODE_MASK", i2c
        )

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
        transport = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()
        port = (ROOT / "soc/t5ai/freertos.c").read_text()
        initialize_soc = startup.index("SystemInit()")
        enable_cache = startup.index("icache_enable()")
        announce = initialization.index("t5ai_cp_mailbox_announce_power_up()")
        enable_irq = initialization.index("__enable_irq()")
        self.assertLess(initialize_soc, enable_cache)
        self.assertLess(announce, enable_irq)
        self.assertNotIn("uart0[7] = 'A'", startup)
        self.assertIn("< 3200u", initialization)
        self.assertIn("t5ai_mailbox_channel_bootstrap_send", mailbox)
        self.assertIn("static void prv_send_reset", transport)
        self.assertIn("MBOX_RESET", transport)
        self.assertIn("dcache_invalidate((void *)address", transport)
        self.assertNotIn("dcache_flush((void *)address", transport)
        self.assertIn("CP_LOG_CHANNEL 0x1eu", mailbox)
        self.assertIn("CP_LOG_ASSERT_OUT_COMMAND 4u", mailbox)
        self.assertIn("s_log_buffer[0] = 1u", mailbox)
        self.assertIn("t5ai_mailbox_channel_send(CP_LOG_CHANNEL", mailbox)
        self.assertIn("xTaskGetSchedulerState() == taskSCHEDULER_RUNNING", mailbox)
        self.assertIn("t5ai_mailbox_channel_bootstrap_send(CP_LOG_CHANNEL", mailbox)
        self.assertIn("prv_wait_for_log_buffer_release(scheduler_running)", mailbox)
        log_release = mailbox.split(
            "static status_t prv_wait_for_log_buffer_release", 1
        )[1].split("status_t t5ai_cp_mailbox_log", 1)[0]
        self.assertIn("dcache_invalidate(s_log_buffer, 1u)", log_release)
        self.assertIn("s_log_buffer[0] == 0u", log_release)
        dbgserial = (ROOT / "src/fw/console/dbgserial_t5ai.c").read_text()
        dbgserial_input = (
            ROOT / "src/fw/console/dbgserial_input_t5ai.c"
        ).read_text()
        dbgserial_common = (ROOT / "src/fw/console/dbgserial.c").read_text()
        dbgserial_input_common = (
            ROOT / "src/fw/console/dbgserial_input.c"
        ).read_text()
        console_build = (ROOT / "src/fw/console/wscript_build").read_text()
        serial_console = (ROOT / "src/fw/console/serial_console.c").read_text()
        self.assertIn("t5ai_cp_mailbox_log(s_t5ai_buffer", dbgserial)
        self.assertNotIn("CONFIG_SOC_T5AI", dbgserial_common)
        self.assertNotIn("CONFIG_SOC_T5AI", dbgserial_input_common)
        self.assertIn("'dbgserial.c'", console_build)
        self.assertIn("'dbgserial_input.c'", console_build)
        self.assertIn("'dbgserial_t5ai.c'", console_build)
        self.assertIn("'dbgserial_input_t5ai.c'", console_build)
        self.assertNotIn("T5AI_CP_LOG_RX_CHANNEL", dbgserial_input)
        self.assertNotIn("T5AI_CP_LOG_USER_INPUT_COMMAND", dbgserial_input)
        self.assertNotIn("prv_log_mailbox_receive", dbgserial_input)
        self.assertIn("frame->address", mailbox)
        self.assertIn("CP_QUEUE_DEPTH 4u", mailbox)
        receive_wait = mailbox.split("status_t t5ai_cp_mailbox_receive", 1)[1].split(
            "status_t t5ai_cp_mailbox_try_receive", 1
        )[0]
        self.assertNotIn("taskYIELD", receive_wait)
        self.assertIn("xSemaphoreTake(s_pm_queue.ready", receive_wait)
        self.assertNotIn("s_queues[", mailbox)
        self.assertNotIn("t5ai_cp_mailbox_process_pending();", main)
        self.assertNotIn("DbgSerialMailboxProbe_", dbgserial_input)
        self.assertNotIn("DbgSerialMailboxProbe_", main)
        self.assertNotIn("mailbox_probe", dbgserial_input)
        self.assertNotIn("mailbox_probe", main)
        self.assertIn("boot_splash_start();", main)
        self.assertIn("prv_qspi_init() && prv_panel_init()", display)
        self.assertIn(
            "prv_register(PANEL_DCS_INVERSION_OFF,NULL,0)&&"
            "prv_register(PANEL_DCS_NORMAL_MODE,NULL,0)",
            "".join(display.split()),
        )
        panel_init = display.split("static bool prv_panel_init(void)", 1)[1].split(
            "void display_init(void)", 1
        )[0]
        self.assertLess(panel_init.index("prv_register(PANEL_DCS_INVERSION_OFF"), panel_init.index("prv_register(PANEL_DCS_DISPLAY_ON"))
        self.assertNotIn("prv_register(0x23u", panel_init)
        self.assertIn("exact compiled Tuya ordering", display)
        self.assertIn(
            "GPIO_BASE[pin]=(GPIO_BASE[pin]&~DISPLAY_GPIO_CONFIG_MASK)|"
            "DISPLAY_GPIO_OUTPUT_DISABLE|DISPLAY_GPIO_SECOND_FUNCTION|DISPLAY_GPIO_DRIVE_MAX",
            "".join(display.split()),
        )
        self.assertIn("*prv_qspi(QSPI_CMD_C_CFG1) = QSPI_COMMAND_C_FIFO_CONFIG", display)
        self.assertNotIn("0x3u << ((length + 4u) * 2u)", display)
        brightness = display.index("{PANEL_DCS_BRIGHTNESS, PANEL_MAX_BRIGHTNESS}")
        sleep_out = display.index("prv_register(PANEL_DCS_SLEEP_OUT")
        self.assertLess(brightness, sleep_out)
        self.assertIn(
            "*prv_qspi(QSPI_CONFIG) = QSPI_CS_HIGH_WAIT | QSPI_ENABLE | QSPI_IO23_ENABLE",
            display,
        )
        self.assertIn("#define QSPI_CS_HIGH_WAIT (8u << 24)", display)
        self.assertIn("SYSTEM_CPU1_INTERRUPT_ENABLE |= QSPI0_INTERRUPT_ENABLE", display)
        self.assertNotIn("NVIC_EnableIRQ(QSPI0_IRQn)", display)
        self.assertIn("*prv_qspi(QSPI_GLB_CTRL) |= QSPI_RESET_RELEASE", display)
        self.assertIn("(QSPI_CLOCK_DIVIDER_80MHZ << QSPI_CLOCK_DIVIDER_SHIFT)", display)
        self.assertIn("*prv_qspi(QSPI_RST_CFG) = 0u", display)
        self.assertNotIn("display_co5300_bitbang_probe", display)
        self.assertNotIn("display_co5300_log_diagnostics", display)
        self.assertNotIn("prv_draw_diagnostic_pattern", display)
        self.assertNotIn("PBLBIT", display)
        self.assertNotIn("PBLDSP", display)
        self.assertNotIn("PBLREG", display)
        self.assertIn("return T5AI_BACKLIGHT_FULL_PERCENT", backlight)
        self.assertNotIn("display_co5300_set_brightness", backlight)
        self.assertIn("Do not issue QSPI commands", backlight)
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
        poll = touch[touch.index("static bool prv_poll") : touch.index("static void prv_touch_task")]
        self.assertIn("s_consecutive_read_failures >= CST92XX_RESET_FAILURE_THRESHOLD", poll)
        self.assertNotIn("i2c_bitbang_recovery(I2C_CST92XX)", poll)
        self.assertIn("prv_reset(true);", poll)
        self.assertIn("CST92XX_TASK_PRIORITY (configMAX_PRIORITIES - 2)", touch)
        read_report_start = touch.index("static bool prv_read_report")
        read_report = touch[
            read_report_start : touch.index("static int16_t prv_map_coordinate", read_report_start)
        ]
        self.assertNotIn("system_task_watchdog_feed();", read_report)
        self.assertIn("CST92XX_REPORT_ACK", touch)
        self.assertNotIn("acknowledge", read_report)
        self.assertEqual(read_report.count("i2c_write_block"), 1)
        self.assertIn('xTaskCreate(prv_touch_task, "CST92xx"', touch)
        self.assertIn("CST92XX_TASK_PRIORITY (configMAX_PRIORITIES - 2)", touch)
        self.assertIn("#define CST92XX_INTERRUPT_PIN 42u", touch)
        self.assertIn("#define CST92XX_RESET_PIN 43u", touch)
        self.assertIn("ExtiTrigger_Falling, prv_interrupt_handler", touch)
        self.assertIn("xSemaphoreGiveFromISR", touch)
        self.assertIn("xSemaphoreTake(s_touch_wake, portMAX_DELAY)", touch)
        self.assertNotIn("vTaskDelay(milliseconds_to_ticks(CST92XX_POLL_MS))", touch)
        self.assertNotIn("system_task_add_callback(prv_poll", touch)
        touch_nav = (ROOT / "src/fw/applib/ui/recognizer/touch_nav.c").read_text()
        self.assertNotIn("state->counters.position_updates++", touch_nav)
        self.assertNotIn("state->counters.widget_started++", touch_nav)
        self.assertNotIn("s_mailbox_touch_nav_request", dbgserial_input)
        self.assertNotIn("touch_get_diagnostics", main)
        self.assertNotIn("event_loop_get_touch_diagnostics", main)
        self.assertNotIn("s_physical_samples", (ROOT / "src/fw/services/touch/touch.c").read_text())
        events = (ROOT / "src/fw/kernel/events.c").read_text()
        system_task = (ROOT / "src/fw/services/system_task/service.c").read_text()
        self.assertNotIn("MAX_SYSTEM_EVENT_SET_EVENTS", events)
        self.assertNotIn("event_get_queue_diagnostics", events)
        event_take = events.split("bool event_take_timeout", 1)[1].split(
            "void **event_get_buffer", 1
        )[0]
        self.assertNotIn("#ifdef CONFIG_SOC_T5AI", event_take)
        self.assertNotIn("(void)timeout_ms", event_take)
        self.assertIn("#define SYSTEM_TASK_PRIORITY (tskIDLE_PRIORITY + 1)", system_task)
        self.assertIn("static SystemTaskEventCallback s_current_cb", system_task)
        idle_watchdog = system_task.split(
            "static void system_task_idle_timer_callback", 1
        )[1].split("static void system_task_main", 1)[0]
        self.assertIn("if (s_system_task_idle", idle_watchdog)
        self.assertIn("uxQueueMessagesWaiting", idle_watchdog)
        splash = (ROOT / "src/fw/services/boot_splash/service.c").read_text()
        t5ai_splash = (
            ROOT / "src/fw/services/boot_splash/service_t5ai.c"
        ).read_text()
        splash_build = (
            ROOT / "src/fw/services/boot_splash/wscript_build"
        ).read_text()
        self.assertNotIn("CONFIG_SOC_T5AI", splash)
        self.assertIn("service_t5ai.c", splash_build)
        self.assertIn("bld.env.CONFIG_SOC_T5AI", splash_build)
        self.assertIn("static uint8_t s_t5ai_splash_row[PBL_DISPLAY_WIDTH]", t5ai_splash)
        self.assertIn("display_init();", t5ai_splash)
        self.assertIn("display_co5300_update_cpu(prv_t5ai_splash_next_row", t5ai_splash)
        self.assertNotIn("delay_us", t5ai_splash)
        self.assertIn("The logo is compiled into the AP image", t5ai_splash)
        self.assertNotIn("kernel_malloc", t5ai_splash)
        t5ai_startup = main.split("#ifdef CONFIG_SOC_T5AI", 2)[2].split("#endif", 1)[0]
        self.assertNotIn("display_init();", t5ai_startup)
        self.assertNotIn("PBLTIM", main)
        self.assertNotIn("event_get_queue_diagnostics", main)
        self.assertNotIn("g_t5ai_boot_stage", main)
        self.assertNotIn("g_t5ai_app_stage", main)
        self.assertNotIn("app_manager_is_initialized()", main)
        self.assertNotIn("app_manager_is_first_app_launched()", main)
        self.assertIn("cron_service_init();", main)
        self.assertIn("accel_manager_init();", main)
        self.assertIn("touch_init();", main)
        self.assertIn("services_normal_init();", main)
        self.assertEqual(main.count("bt_ctl_set_enabled(true);"), 1)
        self.assertLess(
            main.index("services_normal_init();"),
            main.index("bt_ctl_set_enabled(true);"),
        )
        normal_services = (
            ROOT / "src/fw/services/services_normal/service.c"
        ).read_text()
        self.assertIn("persist_service_init();", normal_services)
        self.assertIn("dls_init();", normal_services)
        self.assertIn("app_order_storage_init();", normal_services)
        self.assertIn("music_init();", normal_services)
        self.assertIn("blob_db_init_dbs();", normal_services)
        event_loop = (ROOT / "src/fw/kernel/event_loop.c").read_text()
        self.assertNotIn("app_install_manager_init();", event_loop)
        launcher_menu = (
            ROOT / "src/fw/apps/system/launcher/default/menu_layer.c"
        ).read_text()
        self.assertNotIn("CONFIG_SOC_T5AI", launcher_menu)
        self.assertIn("launcher_app_glance_service_init", launcher_menu)
        self.assertIn("launcher_app_glance_service_draw_glance_for_app_node", launcher_menu)
        self.assertIn("launcher_app_glance_service_play_glance_for_app_node", launcher_menu)
        resource_impl = (ROOT / "src/fw/resource/resource_impl.def").read_text()
        t5ai_resource_impl = resource_impl.split("#if defined(CONFIG_SOC_T5AI)", 1)[1].split(
            "#elif", 1
        )[0]
        self.assertIn("RESOURCE_IMPL(g_app_file_impl)", t5ai_resource_impl)
        self.assertIn("RESOURCE_IMPL(g_builtin_impl)", t5ai_resource_impl)
        self.assertIn("RESOURCE_IMPL(g_system_bank_impl)", t5ai_resource_impl)
        builtin_storage = (ROOT / "src/fw/resource/resource_storage_builtin.c").read_text()
        self.assertNotIn(
            "defined(CONFIG_RECOVERY_FW) || defined(CONFIG_SOC_T5AI)", builtin_storage
        )
        flash_storage = (ROOT / "src/fw/resource/resource_storage_flash.c").read_text()
        flash_driver = (ROOT / "src/fw/drivers/flash/t5ai.c").read_text()
        display_driver = (ROOT / "src/fw/drivers/display/t5ai/display_co5300.c").read_text()
        animation = (ROOT / "src/fw/applib/ui/animation.h").read_text()
        board = (ROOT / "src/fw/board/boards/board_unhygienix.c").read_text()
        mpu = (ROOT / "src/fw/drivers/mpu.c").read_text()
        t5ai_memory = (ROOT / "include/pbl/soc/t5ai/memory.h").read_text()
        self.assertIn("legacy_defective_checksum_memory(", flash_storage)
        self.assertIn("(const void *)(uintptr_t)(BANK.begin + offset)", flash_storage)
        self.assertIn("prv_t5ai_system_bank_metadata_valid", flash_storage)
        self.assertIn("last_entry.resource_id == manifest.num_resources", flash_storage)
        self.assertIn("individual resources retain their existing CRC check", flash_storage)
        self.assertNotIn("T5AI_BOOT_STAGE", event_loop)
        self.assertNotIn("vTaskDelay(5u);", event_loop)
        self.assertNotIn("vTaskDelay(10u);", event_loop)
        self.assertNotIn("taskYIELD();", event_loop)
        self.assertIn("prv_read_cached(address, output, length, &chunk)", flash_driver)
        self.assertIn("T5AI_FLASH_READ_CACHE_LINES 4u", flash_driver)
        self.assertIn("cache complete\n// IPC-sized lines for every flash region", flash_driver)
        self.assertEqual(flash_driver.count("prv_invalidate_read_cache("), 3)
        self.assertIn("prv_pixel_stream_start_dma", display_driver)
        self.assertIn("prv_finish_row_dma", display_driver)
        self.assertIn("display_co5300_update_cpu", display_driver)
        self.assertIn("prv_display_update(nrcb, uccb, false, false);", display_driver)
        self.assertIn("prv_dcache_clean(buffer, length);", display_driver)
        self.assertIn("framebuffer_pixel ^= DISPLAY_COLOR_MASK", display_driver)
        self.assertIn("prv_display_update(nrcb, uccb, use_dma, false);", display_driver)
        self.assertIn("if (source_x != previous_source_x)", display_driver)
        self.assertIn("converted_pixel = s_rgb565[framebuffer_pixel]", display_driver)
        self.assertIn("#if defined(CONFIG_PLATFORM_GABBRO)", animation)
        self.assertNotIn("CONFIG_SOC_T5AI", animation)
        self.assertIn(
            "#define ANIMATION_RENDER_FRAME_INTERVAL_MS ANIMATION_TARGET_FRAME_INTERVAL_MS",
            animation,
        )
        self.assertIn("#define T5AI_DISPLAY_PSRAM_MPU_REGION 10u", t5ai_memory)
        self.assertIn("MpuCachePolicy_WriteBackWriteAllocate", board)
        self.assertIn("mpu_set_region(&s_display_psram_region);", board)
        self.assertIn("prv_enable_display_psram_cache();", board)
        self.assertIn("address >= T5AI_DISPLAY_PSRAM_BASE", mpu)
        self.assertNotIn("CONFIG_SOC_T5AI", event_loop)
        self.assertIn("// emulate a button press-and-release", event_loop)
        shell_event_loop = (ROOT / "src/fw/shell/normal/shell_event_loop.c").read_text()
        self.assertNotIn("T5AI_SHELL_STAGE", shell_event_loop)
        self.assertIn(
            "#else\n      battery_ui_handle_state_change_event",
            shell_event_loop,
        )
        shell_prefs = (ROOT / "src/fw/shell/normal/prefs.c").read_text()
        self.assertIn("#ifndef CONFIG_SOC_T5AI\n  prv_maybe_run_settings", shell_event_loop)
        self.assertIn(
            "#ifndef CONFIG_SOC_T5AI\n  bool factory_reset_or_first_use",
            shell_event_loop,
        )
        app_cache = (ROOT / "src/fw/services/app_cache/service.c").read_text()
        self.assertIn(
            "if (exists && !app_storage_app_exists(app_id)) {",
            app_cache,
        )
        self.assertNotIn(
            "if (exists) {\n      if (!app_storage_app_exists(app_id)) {",
            app_cache,
        )
        pfs = (ROOT / "src/fw/services/filesystem/pfs.c").read_text()
        self.assertIn("if (!pfs_active()) {", pfs)
        self.assertNotIn("const bool active = pfs_active();", pfs)
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
            "#ifndef CONFIG_RECOVERY_FW\n"
            "  services_normal_set_runlevel(runlevel);",
            services,
        )
        self.assertNotIn(
            "!defined(CONFIG_RECOVERY_FW) && !defined(CONFIG_SOC_T5AI)", services
        )
        self.assertNotIn("CONFIG_SOC_T5AI", services_common)
        self.assertIn(".set_enable_fn = accel_manager_enable", services_common)
        self.assertIn(".set_enable_fn = light_allow", services_common)
        self.assertIn(".set_enable_fn = vibe_service_set_enabled", services_common)
        self.assertIn("#ifdef CONFIG_HRM", services_common)
        self.assertIn(".set_enable_fn = hrm_manager_enable", services_common)
        self.assertIn("persist_service_client_open", process_manager)
        self.assertIn("persist_service_client_close", process_manager)
        self.assertNotIn("if (dls_initialized())", process_manager)
        self.assertIn(
            "app_db_enumerate_entries(prv_app_install_enumerate_app_db, &cb_data);",
            app_install_manager,
        )
        self.assertNotIn("#ifdef CONFIG_SOC_T5AI", app_order_storage)
        self.assertNotIn("T5AI_APP_STAGE", app_manager)
        self.assertNotIn("T5AI_LAUNCHER_STAGE", launcher)
        self.assertNotIn("T5AI_APP_STATE_STAGE", app_state)
        self.assertNotIn("t5ai_rtc_get_raw_ticks()", main)
        self.assertNotIn("SysTick->LOAD", main)
        self.assertNotIn("s_character_callback", dbgserial_input)
        self.assertIn("dbgserial_putchar_lazy(*(msg++))", serial_console)
        self.assertIn("CP_CONTROL_CHANNEL 0x10u", mailbox)
        self.assertIn("CP_POWER_UP_COMMAND 1u", mailbox)
        self.assertIn("CP_HEARTBEAT_COMMAND 2u", mailbox)
        self.assertIn("CP_STARTUP_TIMEOUT_MS 2000u", mailbox)
        self.assertIn("((uint32_t)s_control_sequence << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT)", mailbox)
        self.assertIn("response->parameter1 == 0u", mailbox)
        self.assertIn("CP_PM_TX_CHANNEL 0x12u", mailbox)
        self.assertIn("CP_PM_RX_CHANNEL 0x42u", mailbox)
        self.assertNotIn(
            "message->header >> T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) == s_control_sequence", mailbox
        )
        self.assertIn("void vApplicationTickHook(void)", port)
        self.assertNotIn("t5ai_cp_mailbox_heartbeat();", port)
        self.assertNotIn("t5ai_cp_mailbox_heartbeat();", main)
        self.assertNotIn("cp_heartbeat_timer", main)
        self.assertIn("xTaskCreate(prv_heartbeat_task", mailbox)
        self.assertIn("CP_HEARTBEAT_PERIOD_MS 1000u", mailbox)
        self.assertIn("vTaskDelayUntil(&last_wake", mailbox)
        self.assertLess(
            main.index("t5ai_cp_mailbox_enable_rx_interrupt();"),
            main.index("resource_init();"),
        )
        heartbeat = mailbox.split("void t5ai_cp_mailbox_heartbeat(void)", 1)[1]
        heartbeat = heartbeat.split("static status_t prv_pm_request", 1)[0]
        self.assertIn("t5ai_mailbox_channel_send_async(", heartbeat)
        self.assertIn("s_heartbeat_in_flight", heartbeat)
        self.assertIn("sizeof(s_heartbeat_parameter)", heartbeat)
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

    def test_t5ai_heartbeat_repeats_after_complete_physical_ack(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        callback = mailbox.split("static void prv_heartbeat_complete(", 1)[1]
        callback = "static void prv_heartbeat_complete(" + callback.split(
            "\nvoid t5ai_cp_mailbox_heartbeat", 1
        )[0]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int status_t;
#define PASSED(s) ((s) >= 0)
#define FAILED(s) ((s) < 0)
#define CP_ACK_STATE_PENDING 1u
#define CP_ACK_STATE_COMPLETE 2u
typedef struct {
  uint32_t header, parameter1, parameter2, parameter3;
} T5AIMailboxMessage;
static bool s_heartbeat_in_flight;
""" + callback + r"""
int main(void) {
  T5AIMailboxMessage ack = {.header = 2u, .parameter3 = 2u};
  /* CPU0 completes each heartbeat directly; no logical response follows. */
  for (unsigned i = 0; i < 10; ++i) {
    assert(!s_heartbeat_in_flight);
    s_heartbeat_in_flight = true;
    prv_heartbeat_complete(&ack, 0, NULL);
    assert(!s_heartbeat_in_flight);
  }
  ack.parameter3 = 1u;
  s_heartbeat_in_flight = true;
  prv_heartbeat_complete(&ack, 0, NULL);
  assert(s_heartbeat_in_flight);
  ack.parameter3 = 3u;
  prv_heartbeat_complete(&ack, 0, NULL);
  assert(!s_heartbeat_in_flight);
  s_heartbeat_in_flight = true;
  prv_heartbeat_complete(&ack, -1, NULL);
  assert(!s_heartbeat_in_flight);
  s_heartbeat_in_flight = true;
  prv_heartbeat_complete(NULL, 0, NULL);
  assert(!s_heartbeat_in_flight);
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "heartbeat.c"
            binary = Path(directory) / "heartbeat"
            source.write_text(harness)
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(source), "-o", str(binary)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_t5ai_mailbox_physical_gate_and_bootstrap_interleaving(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        source = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()

        def function(name):
            start = source.index(name + "(")
            start = source.rfind("\n", 0, start) + 1
            brace = source.index("{", start)
            depth = 1
            end = brace + 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[start:end]

        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int status_t;
#define S_SUCCESS 0
#define E_BUSY -1
#define E_ERROR -2
#define E_INVALID_OPERATION -3
#define E_INVALID_ARGUMENT -4
#define E_AGAIN -5
#define T5AI_MAILBOX_CHANNEL_COUNT 16u
#define T5AI_MAILBOX_TX_CHANNEL_BASE 0x10u
#define T5AI_MAILBOX_RX_CHANNEL_BASE 0x40u
#define MAILBOX_BOOT_POLL_MS 100u
#define MBOX_ACK_BOX (1u << 12)
#define MBOX_SYNC_TX (2u << 12)
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED (1u << 8)
#define MBOX_CH1_CTRL  (rx_head < rx_count ? MBOX_FIFO_NOT_EMPTY : 0u)
#define MBOX_FIFO_NOT_EMPTY (1u << 19)
#define T5AI_AON_RTC_COUNT_LOW (++now)
typedef struct { uint32_t header, parameter1, parameter2, parameter3; } T5AIMailboxMessage;
typedef void (*T5AIMailboxTransmitHandler)(const T5AIMailboxMessage *, status_t, void *);
typedef void (*T5AIMailboxReceiveHandler)(T5AIMailboxMessage *, void *);
typedef enum { TxState_Idle, TxState_Pending, TxState_Active, TxState_Complete } TxState;
typedef struct {
 T5AIMailboxMessage message, ack;
 T5AIMailboxTransmitHandler handler;
 void *context;
 void *completion;
 TxState state;
 bool synchronous;
 uint8_t sequence;
 uint32_t active_start;
} TxChannel;
typedef struct { T5AIMailboxReceiveHandler handler; void *context; } RxChannel;
typedef struct { T5AIMailboxMessage message; } MailboxOwnedSlot;
static TxChannel s_tx[16];
static RxChannel s_rx[16];
static MailboxOwnedSlot s_ack_slots[16];
static uint8_t s_sequence, s_next_tx_channel;
static uint16_t s_pending_ack_mask;
static bool s_faulted;
static unsigned now, published_count, signalled, rx_head, rx_count;
static T5AIMailboxMessage published[32], incoming[4];
static int xSemaphoreGive(void *p) { (void)p; ++signalled; return 1; }
static uint32_t prv_bootstrap_ticks(uint32_t ms) { return ms; }
static status_t prv_publish(const T5AIMailboxMessage *m) {
 assert(published_count < 32); published[published_count++] = *m; return S_SUCCESS;
}
static bool prv_snapshot_rx(T5AIMailboxMessage *m) {
 if (rx_head == rx_count) return false;
 *m = incoming[rx_head++]; return true;
}
static void prv_bootstrap_resynchronize(uint32_t ms) { (void)ms; }
static bool prv_bootstrap_publish(const T5AIMailboxMessage *m, uint32_t ms) {
 (void)ms; return prv_publish(m) == S_SUCCESS;
}
"""
        # Reuse production wire constants; keep test frames literal to check the ABI.
        harness += (ROOT / "include/pbl/soc/t5ai/memory.h").read_text().replace("#pragma once", "")
        harness += (ROOT / "include/pbl/soc/t5ai/mailbox_protocol.h").read_text().replace("#pragma once", "")
        harness += source[source.index("#define MAILBOX_HEADER_COMMAND_MASK"):
                          source.index("#define MBOX_BASE")]
        for name in ("prv_any_active", "prv_tx_channel", "prv_rx_channel",
                     "prv_finish_tx", "prv_process_ack", "prv_process_command",
                     "prv_publish_pending_acks", "prv_schedule_tx",
                     "t5ai_mailbox_channel_bootstrap_send"):
            harness += "\n" + function(name)
        harness += r"""
int main(void) {
 /* Concurrent logical producers must produce just one physical command. */
 s_tx[0].state = s_tx[2].state = s_tx[14].state = TxState_Pending;
 assert(prv_schedule_tx() == S_SUCCESS);
 assert(published_count == 1 && s_tx[0].state == TxState_Active);
 assert(prv_schedule_tx() == S_SUCCESS && published_count == 1);
 /* Physical ACK bypasses the command gate without retiring our command. */
 T5AIMailboxMessage command = {.header = (0x46u << 24) | (7u << 16) | 3u};
 prv_process_command(&command);
 assert(published_count == 2 && (published[1].header & MBOX_ACK_BOX));
 assert(s_tx[0].state == TxState_Active);
 T5AIMailboxMessage ack = published[0];
 ack.header |= MBOX_ACK_BOX;
 prv_process_ack(&ack);
 assert(s_tx[0].state == TxState_Idle);
 s_tx[0].state = TxState_Pending;
 assert(prv_schedule_tx() == S_SUCCESS);
 assert((published[2].header >> 24) == 0x12u);
 /* Wrong sequence cannot release the physical gate. */
 ack = published[2]; ack.header ^= 1u << 16; ack.header |= MBOX_ACK_BOX;
 prv_process_ack(&ack);
 assert(s_tx[2].state == TxState_Active);
 ack = published[2]; ack.header |= MBOX_ACK_BOX;
 s_tx[2].synchronous = true;
 prv_process_ack(&ack);
 assert(s_tx[2].state == TxState_Complete && signalled == 1);
 assert(prv_schedule_tx() == S_SUCCESS);
 assert((published[3].header >> 24) == 0x1eu);
 /* The completed synchronous slot cannot be selected again. */
 assert(s_tx[2].state == TxState_Complete);
 /* Bootstrap must ACK an interleaved command before consuming its own ACK. */
 memset(s_tx, 0, sizeof(s_tx));
 published_count = 0; s_sequence = 20;
 incoming[0] = (T5AIMailboxMessage){.header = (0x46u << 24) | (8u << 16) | 3u};
 incoming[1] = (T5AIMailboxMessage){.header = (0x10u << 24) | (21u << 16) | MBOX_ACK_BOX | 1u};
 rx_count = 2; rx_head = 0;
 T5AIMailboxMessage boot = {.header = 1u};
 assert(t5ai_mailbox_channel_bootstrap_send(0x10u, &boot, &ack, 100u) == S_SUCCESS);
 assert(rx_head == 2 && published_count == 2);
 assert((published[1].header & MBOX_ACK_BOX) != 0u);
 assert((published[1].header & T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED) != 0u);
 /* Vendor fire-and-forget synchronous physical traffic gets no ACK. */
 command.header = (0x46u << 24) | MBOX_SYNC_TX | 3u;
 prv_process_command(&command);
 assert(published_count == 2);
 assert(prv_publish_pending_acks());
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            c_file = Path(directory) / "mailbox.c"
            binary = Path(directory) / "mailbox"
            c_file.write_text(harness)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(c_file), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)
        task = function("prv_mailbox_task")
        self.assertLess(task.index("prv_drain_rx()"), task.index("prv_expired_channel()"))
        self.assertIn("rx_pending ? -1 : prv_expired_channel()", task)

    def test_t5ai_mailbox_clients_quarantine_ambiguous_ownership(self):
        physical = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        mb_ipc = (ROOT / "src/fw/drivers/t5ai/mb_ipc.c").read_text()
        flash = (ROOT / "src/fw/drivers/flash/t5ai_flash_ipc.c").read_text()
        battery = (ROOT / "src/fw/drivers/battery/t5ai.c").read_text()
        dbgserial = (ROOT / "src/fw/console/dbgserial_t5ai.c").read_text()
        reset = (ROOT / "src/fw/drivers/watchdog/t5ai.c").read_text()

        physical_init = physical.split("status_t t5ai_mailbox_channel_init", 1)[1].split(
            "status_t t5ai_mailbox_channel_start", 1
        )[0]
        self.assertNotIn("NVIC_EnableIRQ", physical_init)
        self.assertIn("t5ai_force_whole_chip_reset(T5AI_RESET_REASON_MAILBOX_BASE | channel);",
                      physical)
        self.assertIn("#define MAILBOX_ACTIVE_TIMEOUT_MS 5000u", physical)
        self.assertIn("RebootReasonCode_MailboxTimeout", physical)
        self.assertIn("#define MB_IPC_PHYSICAL_TIMEOUT_MS 5000u", mb_ipc)
        self.assertIn("#define CP_RUNTIME_TIMEOUT_MS 5000u", mailbox)
        self.assertIn("#define IPC_TIMEOUT_MS 5000u", flash)
        self.assertLess(
            mailbox.index("t5ai_mailbox_channel_register(channel"),
            mailbox.index("t5ai_mailbox_channel_start()"),
        )
        self.assertIn("socket->poisoned = true", mb_ipc)
        self.assertIn("MB_IPC_PHYSICAL_TIMEOUT_MS);", mb_ipc)
        self.assertNotIn("timeout_ms < MB_IPC_PHYSICAL_TIMEOUT_MS", mb_ipc)
        self.assertIn("const bool ambiguous = status == E_AGAIN", flash)
        self.assertIn("ignored stale completion", flash)
        self.assertNotIn("reconnect", flash)
        self.assertIn("t5ai_cp_mailbox_shared_range_valid", flash)
        battery_get = battery.split("int battery_get_millivolts(void)", 1)[1].split(
            "int battery_get_constants", 1
        )[0]
        self.assertIn("__atomic_load_n", battery_get)
        self.assertNotIn("t5ai_saradc_read_peak", battery_get)
        self.assertIn("xTaskCreate(prv_battery_task", battery)
        self.assertIn("#define T5AI_BATTERY_STARTUP_DELAY_MS 30000u", battery)
        self.assertIn("s_t5ai_mutex", dbgserial)
        self.assertIn("T5AI_AON_PMU_R2", reset)

    def test_t5ai_holds_psram_power_for_the_compositor_framebuffer(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        compositor = (
            ROOT / "src/fw/services/compositor/compositor.c"
        ).read_text()
        self.assertNotIn("0x2809f700u", initialization)
        self.assertIn("CP_PM_PSRAM_POWER_COMMAND 7u", mailbox)
        self.assertIn("CP_PM_PSRAM_APP_MODULE 9u", mailbox)
        vote = compositor.index("t5ai_cp_mailbox_enable_psram()")
        validate = compositor.index("prv_validate_t5ai_framebuffer_ram()", vote)
        self.assertLess(vote, validate)

    def test_t5ai_holds_cp_awake_until_low_voltage_resume_is_supported(self):
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        main = (ROOT / "src/fw/main.c").read_text()
        self.assertIn("CP_PM_SLEEP_COMMAND 3u", mailbox)
        self.assertIn("CP_PM_SLEEP_APP_MODULE 12u", mailbox)
        self.assertIn("t5ai_cp_mailbox_vote_app_sleep", mailbox)
        main_init = main.index("static NOINLINE void prv_main_task_init")
        vote = main.index("t5ai_cp_mailbox_vote_app_sleep(false)", main_init)
        self.assertLess(main.index("init_drivers();", main_init), vote)
        self.assertLess(vote, main.index("prv_t5ai_bluetooth_init();", main_init))
        self.assertNotIn("PBLPM", main)

    def test_t5ai_core_clock_is_an_explicit_systick_contract(self):
        kconfig = (ROOT / "soc/Kconfig").read_text()
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        port = (ROOT / "soc/t5ai/freertos.c").read_text()
        main = (ROOT / "src/fw/main.c").read_text()
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()
        transport = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()
        self.assertIn("config T5AI_CORE_CLOCK_HZ", kconfig)
        self.assertIn("default 120000000", kconfig)
        self.assertIn(
            "SystemCoreClock = CONFIG_T5AI_CORE_CLOCK_HZ", initialization
        )
        self.assertIn("SystemCoreClock % RTC_TICKS_HZ", port)
        self.assertIn("reload <= SysTick_LOAD_RELOAD_Msk", port)
        self.assertIn("t5ai_freertos_apply_systick_clock", port)
        self.assertIn("SysTick->LOAD = reload", port)
        self.assertIn("return false;", port)
        self.assertIn("t5ai_freertos_apply_systick_clock();", main)
        self.assertIn("t5ai_cp_mailbox_enable_rx_interrupt();", main)
        self.assertIn("void MAILBOX_IRQHandler(void)", transport)
        self.assertIn("xSemaphoreGiveFromISR", transport)
        self.assertNotIn("system_task_add_callback_from_isr", transport)
        self.assertIn("MBOX_CH1_CTRL |= MBOX_INTERRUPT_ENABLE", transport)
        self.assertIn("MAILBOX_SYSTEM_INTERRUPT_ENABLE", transport)
        self.assertIn("CP_PM_CPU_FREQUENCY_COMMAND 4u", mailbox)
        self.assertIn("CP_PM_CPU1_MODULE 38u", mailbox)
        self.assertIn("t5ai_cp_mailbox_vote_cpu1_frequency", mailbox)
        power = (ROOT / "soc/t5ai/power.c").read_text()
        self.assertIn("T5AICpuFrequency_240MHz", power)
        self.assertNotIn("t5ai_cp_mailbox_vote_cpu1_frequency", main)
        vote = main.index("t5ai_freertos_apply_systick_clock();")
        self.assertLess(main.index("init_drivers();"), vote)
        self.assertLess(vote, main.index("display_init();", vote))
        self.assertNotIn("PBLCLK", main)
        self.assertNotIn("t5ai_clock_get_diagnostics", main)
        self.assertNotIn(
            "T5AI_CP_LOG_RX_CHANNEL",
            (ROOT / "src/fw/console/dbgserial_input_t5ai.c").read_text(),
        )

    def test_t5ai_debug_uart_keeps_the_board_runtime_configuration(self):
        console = (ROOT / "src/fw/console/dbgserial_t5ai.c").read_text()
        driver = (ROOT / "src/fw/drivers/uart/t5ai.c").read_text()
        self.assertIn("t5ai_cp_mailbox_log", console)
        self.assertNotIn("uart_set_baud_rate", console)
        self.assertIn("uart->global_ctrl = UART_GLOBAL_RESET_RELEASE | UART_GLOBAL_CLOCK_GATE_BYPASS;", driver)
        self.assertIn(
            "T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE",
            driver,
        )
        self.assertIn(
            "T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_UART0_INTERRUPT_ENABLE",
            driver,
        )
        self.assertIn("UART_INT_RX_FINISH", driver)

    def test_t5ai_cp_debug_logging_never_blocks_in_an_interrupt(self):
        console = (ROOT / "src/fw/console/dbgserial_t5ai.c").read_text()
        mailbox = (ROOT / "src/fw/drivers/t5ai/cp_mailbox.c").read_text()

        putchar = console.split("void dbgserial_putchar_lazy", 1)[1].split(
            "void dbgserial_flush", 1
        )[0]
        flush = console.split("void dbgserial_flush", 1)[1].split(
            "void dbgserial_putstr_fmt", 1
        )[0]
        cp_log = mailbox.split("status_t t5ai_cp_mailbox_log", 1)[1]
        self.assertIn("if (mcu_state_is_isr())", putchar)
        self.assertIn("if (mcu_state_is_isr())", flush)
        self.assertIn("if (mcu_state_is_isr())", cp_log)
        self.assertIn("return E_INVALID_OPERATION;", cp_log)

    def test_t5ai_enables_global_interrupts_at_first_task_entry(self):
        main = (ROOT / "src/fw/main.c").read_text()
        task_entry = main.index("static void main_task(")
        enable_irq = main.index("__enable_irq()", task_entry)
        initialize = main.index("prv_main_task_init();", task_entry)
        self.assertLess(task_entry, enable_irq)
        self.assertLess(enable_irq, initialize)

    def test_t5ai_leaves_cp_owned_watchdog_disabled(self):
        board = (ROOT / "src/fw/board/boards/board_unhygienix.c").read_text()
        early_init = board.split("void board_early_init(void)", 1)[1]
        early_init = early_init.split("void board_init(void)", 1)[0]
        self.assertIn("watchdog_init();", early_init)
        self.assertNotIn("watchdog_start();", early_init)

    def test_t5ai_kernelbg_keeps_stock_queue_path_with_safe_priority_ordering(self):
        service = (ROOT / "src/fw/services/system_task/service.c").read_text()
        app_manager = (ROOT / "src/fw/process_management/app_manager.c").read_text()
        self.assertIn("#define SYSTEM_TASK_PRIORITY (tskIDLE_PRIORITY + 1)", service)
        self.assertIn("xQueueSelectFromSet(s_system_task_queue_set, portMAX_DELAY)", service)
        self.assertNotIn("CONFIG_SOC_T5AI", service)
        self.assertNotIn("CONFIG_SOC_T5AI", app_manager)
        self.assertIn(".uxPriority = APP_TASK_PRIORITY | portPRIVILEGE_BIT", app_manager)
        board_t5ai = (ROOT / "src/fw/board/boards/board_unhygienix.c").read_text()
        self.assertNotIn("T5AI_APP_PRIORITY", board_t5ai)
        self.assertNotIn("T5AISched", board_t5ai)
        self.assertNotIn("vTaskPrioritySet", board_t5ai)
        t5ai_discovery = (
            ROOT / "src/bluetooth-fw/nimble/gatt_client_discovery_t5ai.c"
        ).read_text()
        self.assertIn("GATTService *gatt_service = kernel_zalloc_check(size_bytes);", t5ai_discovery)
        self.assertNotIn("pvPortMalloc(size_bytes)", t5ai_discovery)
        self.assertNotIn("Skipping remote GATT service", t5ai_discovery)
        task = service.split("static void system_task_main", 1)[1]
        task = task.split("void system_task_init", 1)[0]
        self.assertNotIn("CONFIG_SOC_T5AI", task)
        self.assertIn("xQueueSelectFromSet(s_system_task_queue_set, portMAX_DELAY)", task)
        self.assertIn("xQueueReceive(activated_queue, &event, 0)", task)
        self.assertIn("system_task_watchdog_feed();", task)
        self.assertNotIn("s_system_task_wake", service)

    def test_t5ai_initializes_native_button_debouncing(self):
        main = (ROOT / "src/fw/main.c").read_text()
        exti = (ROOT / "src/fw/drivers/exti/t5ai.c").read_text()
        dbgserial_input = (
            ROOT / "src/fw/console/dbgserial_input_t5ai.c"
        ).read_text()
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
        self.assertIn("T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_I2C0_INTERRUPT_ENABLE", i2c)
        self.assertNotIn("T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE", i2c)
        enable = i2c.split("void i2c_hal_enable", 1)[1].split(
            "void i2c_hal_disable", 1
        )[0]
        self.assertIn("delay_us(T5AI_I2C_ENABLE_SETTLE_US);", enable)
        start = i2c.split("void i2c_hal_start_transfer", 1)[1].split(
            "void i2c_irq_handler", 1
        )[0]
        self.assertIn("delay_us(T5AI_I2C_BUS_FREE_US);", start)
        disable = i2c.split("void i2c_hal_disable", 1)[1].split(
            "bool i2c_hal_is_busy", 1
        )[0]
        self.assertNotIn("i2c_hal_abort_transfer", disable)
        self.assertIn("~T5AI_I2C_CONFIG_ENABLE", disable)
        self.assertIn("NVIC_SetPriority(bus->hal->irqn, T5AI_I2C_IRQ_PRIORITY)", i2c)
        common_i2c = (ROOT / "src/fw/drivers/i2c/common.c").read_text()
        self.assertIn("#define I2C_ERROR_TIMEOUT_MS  (1000)", common_i2c)
        self.assertIn("#define I2C_NACK_COUNT_MAX    (1000)", common_i2c)
        self.assertNotIn("CONFIG_SOC_T5AI", common_i2c)
        self.assertNotIn("prv_should_log_transfer_failure", common_i2c)
        self.assertIn("mutex_lock(bus->state->bus_mutex);", common_i2c)
        self.assertIn("if (!prv_external_rtc_read_raw(&control, registers))", rtc)
        self.assertIn("prv_set_retained_time(external_time);", rtc)
        self.assertIn("prv_external_rtc_write(fallback_time)", rtc)
        self.assertIn("T5AI_RTC_MIN_VALID_TIME", rtc)
        self.assertIn("#define T5AI_RTC_MIN_VALID_TIME 1577836800u", rtc)
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
        self.assertIn("#define T5AI_AON_RTC_HZ 32000u",
                      (ROOT / "include/pbl/soc/t5ai/rtc.h").read_text())
        self.assertIn(
            "return prv_raw_ticks() / (T5AI_AON_RTC_HZ / RTC_TICKS_HZ);",
            implementation,
        )
        self.assertIn("elapsed / T5AI_AON_RTC_HZ", implementation)


    def test_t5ai_retained_state_implementations_are_soc_local(self):
        system_build = (ROOT / "src/fw/system/wscript_build").read_text()
        soc_build = (ROOT / "soc/t5ai/wscript_build").read_text()
        shared_bootbits = (ROOT / "src/fw/system/bootbits.c").read_text()
        shared_reboot_reason = (ROOT / "src/fw/system/reboot_reason.c").read_text()
        t5ai_bootbits = (ROOT / "soc/t5ai/bootbits.c").read_text()
        t5ai_reboot_reason = (ROOT / "soc/t5ai/reboot_reason.c").read_text()

        self.assertNotIn("CONFIG_SOC_T5AI", shared_bootbits)
        self.assertNotIn("CONFIG_SOC_T5AI", shared_reboot_reason)
        self.assertIn("excludes = ['bootbits.c', 'reboot_reason.c']", system_build)
        self.assertIn("'bootbits.c'", soc_build)
        self.assertIn("'reboot_reason.c'", soc_build)
        self.assertIn("t5ai_retained_init();", t5ai_bootbits)
        self.assertIn("t5ai_retained_write(REBOOT_REASON_REGISTER_1", t5ai_reboot_reason)


if __name__ == "__main__":
    unittest.main()
