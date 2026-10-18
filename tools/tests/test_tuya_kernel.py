# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaKernelCheckTest(unittest.TestCase):
    def test_t5ai_safe_reset_requests_cpu0_whole_chip_reboot(self):
        reset = (ROOT / "fw/kernel/reset.c").read_text()
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

    def test_t5ai_uses_stock_fault_handler_with_optional_local_trace(self):
        handler = (ROOT / "fw/hardfault_handler.c").read_text()
        build = (ROOT / "soc/t5ai/CMakeLists.txt").read_text()
        self.assertNotIn("CONFIG_SOC_T5AI", handler)
        self.assertIn("if(CONFIG_T5AI_FAULT_TRACE)", build)
        self.assertIn("HardFault_Handler=t5ai_stock_HardFault_Handler", build)
        self.assertIn("fault_trace.c", build)

    def test_t5ai_cacheable_mpu_regions_are_non_shareable(self):
        mpu = (ROOT / "kernel/arch/arm/mpu_armv8m.c").read_text()
        self.assertIn("#ifdef CONFIG_SOC_T5AI", mpu)
        self.assertIn("region->cache_policy != MpuCachePolicy_NotCacheable", mpu)
        self.assertIn("shareability = ARM_MPU_SH_NON", mpu)

    def test_t5ai_reclaims_only_measured_gabbro_runtime_headroom(self):
        defaults = (ROOT / "soc/t5ai/Kconfig.defconfig").read_text()
        self.assertIn("config APP_RAM_GABBRO_RUNTIME_SIZE", defaults)
        self.assertIn("default 77824", defaults)
        self.assertIn("measured\n# AppState is 73,936 bytes", defaults)

    def test_t5ai_flash_writes_are_limited_to_data_partitions(self):
        flash = (ROOT / "drivers/flash/t5ai.c").read_text()
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

    def test_t5ai_bluetooth_uses_common_service_initialization(self):
        services = (ROOT / "fw/services/services_common/service.c").read_text()
        main = (ROOT / "fw/main.c").read_text()
        self.assertNotIn("CONFIG_SOC_T5AI", services)
        self.assertLess(services.index("shared_prf_storage_init();"),
                        services.index("bt_persistent_storage_init();"))
        self.assertLess(services.index("bt_persistent_storage_init();"),
                        services.index("bt_ctl_init();"))
        self.assertIn("comm_default_kernel_sender_init();", services)
        self.assertIn("put_bytes_init();", services)
        self.assertIn("services_init();", main)
        build = (ROOT / "fw/services/bluetooth/CMakeLists.txt").read_text()
        self.assertIn("bluetooth_persistent_storage_prf.c", build)
        self.assertNotIn("bluetooth_persistent_storage_t5ai.c", build)

    def test_t5ai_shared_i2c_bus_uses_cst92xx_qualified_standard_mode(self):
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        self.assertIn("I2C0_IRQn, 100000u, 20, 21", board)
        self.assertNotIn(".frequency = 400000u", board)


    def test_t5ai_i2c_uses_stable_single_byte_interrupt_mode(self):
        i2c = (ROOT / "drivers/i2c/t5ai.c").read_text()
        self.assertNotIn("static uint32_t prv_read_fifo_count", i2c)
        self.assertNotIn("static uint32_t prv_write_fifo_space", i2c)
        self.assertIn("transfer->data[prv_dev(bus)->state->index++] = (uint8_t)regs->data;", i2c)
        self.assertIn("regs->data = transfer->data[prv_dev(bus)->state->index++];", i2c)
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

    def test_t5ai_announces_ap_startup_before_enabling_interrupts(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        early = initialization.split("void pbl_soc_early_init(void)", 1)[1]
        self.assertLess(early.index("SystemInit();"), early.index("icache_enable();"))
        self.assertLess(early.index("dcache_enable();"),
                        early.index("t5ai_cp_mailbox_announce_power_up();"))
        self.assertLess(early.index("t5ai_cp_mailbox_announce_power_up();"),
                        early.index("__enable_irq();"))
        mailbox = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        self.assertRegex(mailbox, r"CP_CONTROL_CHANNEL\s+0x10u")
        self.assertRegex(mailbox, r"CP_POWER_UP_COMMAND\s+1u")
        self.assertRegex(mailbox, r"CP_HEARTBEAT_COMMAND\s+2u")
        self.assertIn("t5ai_cp_mailbox_heartbeat_poll", mailbox)
        self.assertNotIn("prv_heartbeat_task", mailbox)

    def test_t5ai_display_preserves_qualified_panel_and_qspi_setup(self):
        display = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
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

    def test_t5ai_heartbeat_repeats_after_complete_physical_ack(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        mailbox = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        callback = mailbox.split("static void prv_heartbeat_complete(", 1)[1]
        callback = "static void prv_heartbeat_complete(" + callback.split(
            "\nstatic void prv_heartbeat", 1
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
#define T5RebootTrace_HeartbeatAck 1
static void t5ai_reboot_trace_event(unsigned event, uint32_t data) {
  (void)event; (void)data;
}
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
        source = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()

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
 struct pbl_sem { unsigned unused; } completion;
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
static int pbl_sem_give(struct pbl_sem *p) { (void)p; ++signalled; return 1; }
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
 assert(s_tx[2].state == TxState_Complete && signalled == 2);
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
        physical = (ROOT / "drivers/t5ai/mailbox_channel.c").read_text()
        mailbox = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        mb_ipc = (ROOT / "drivers/t5ai/mb_ipc.c").read_text()
        flash = (ROOT / "drivers/flash/t5ai_flash_ipc.c").read_text()
        battery = (ROOT / "drivers/battery/t5ai.c").read_text()
        dbgserial = (ROOT / "fw/console/dbgserial_t5ai.c").read_text()
        reset = (ROOT / "drivers/watchdog/t5ai.c").read_text()

        physical_init = physical.split("status_t t5ai_mailbox_channel_init", 1)[1].split(
            "status_t t5ai_mailbox_channel_start", 1
        )[0]
        self.assertNotIn("NVIC_EnableIRQ", physical_init)
        self.assertIn("t5ai_force_whole_chip_reset(T5AI_RESET_REASON_MAILBOX_BASE | channel);",
                      physical)
        self.assertRegex(physical, r"#define MAILBOX_ACTIVE_TIMEOUT_MS\s+5000u")
        self.assertIn("RebootReasonCode_MailboxTimeout", physical)
        self.assertRegex(mb_ipc, r"#define MB_IPC_PHYSICAL_TIMEOUT_MS\s+5000u")
        self.assertRegex(mailbox, r"#define CP_RUNTIME_TIMEOUT_MS\s+5000u")
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
        self.assertNotIn("t5ai_saradc_read_peak", battery_get)
        self.assertNotIn("pbl_thread_create", battery)
        self.assertNotIn("pbl_thread_sleep", battery)
        self.assertIn("s_t5ai_mutex", dbgserial)
        self.assertIn("T5AI_AON_PMU_R2", reset)

    def test_t5ai_holds_psram_power_before_board_validation(self):
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        init = board.split("void board_init(void)", 1)[1]
        self.assertLess(init.index("t5ai_cp_mailbox_enable_psram()"),
                        init.index("PBL_ASSERTN(prv_validate_framebuffer_ram())"))
        main = (ROOT / "fw/main.c").read_text()
        self.assertLess(main.index("init_drivers();"), main.index("compositor_init();"))

    def test_t5ai_holds_cp_awake_until_low_voltage_resume_is_supported(self):
        mailbox = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        self.assertRegex(mailbox, r"CP_PM_SLEEP_COMMAND\s+3u")
        self.assertRegex(mailbox, r"CP_PM_SLEEP_APP_MODULE\s+12u")
        init = board.split("void board_init(void)", 1)[1]
        self.assertLess(init.index("t5ai_cp_mailbox_vote_app_sleep(false)"),
                        init.index("t5ai_power_init();"))

    def test_t5ai_clock_and_tick_ownership_is_soc_local(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        tick = (ROOT / "soc/t5ai/tick.c").read_text()
        power = (ROOT / "soc/t5ai/power.c").read_text()
        main = (ROOT / "fw/main.c").read_text()
        self.assertIn("SystemCoreClock = CONFIG_T5AI_CORE_CLOCK_HZ", initialization)
        self.assertIn("t5ai_power_sync_tick_clock();", tick)
        self.assertIn("pbl_kernel_tick_isr();", tick)
        self.assertIn("T5AICpuFrequency_240MHz", power)
        self.assertNotIn("SysTick->", main)
        self.assertNotIn("CONFIG_SOC_T5AI", main)

    def test_t5ai_debug_uart_keeps_the_board_runtime_configuration(self):
        console = (ROOT / "fw/console/dbgserial_t5ai.c").read_text()
        driver = (ROOT / "drivers/uart/t5ai.c").read_text()
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

    def test_t5ai_cp_logging_rejects_interrupt_context(self):
        mailbox = (ROOT / "drivers/t5ai/cp_mailbox.c").read_text()
        cp_log = mailbox.split("status_t t5ai_cp_mailbox_log", 1)[1]
        rejection = cp_log.index("return E_INVALID_OPERATION;")
        self.assertLess(cp_log.index("mcu_state_is_isr()"), rejection)
        self.assertLess(rejection, cp_log.index("pbl_mutex_lock("))

    def test_t5ai_interrupt_enable_is_owned_by_soc_early_init(self):
        initialization = (ROOT / "soc/t5ai/init.c").read_text()
        reset, early = initialization.split("void pbl_soc_early_init(void)", 1)
        self.assertIn("__disable_irq();", reset)
        self.assertNotIn("__enable_irq();", reset)
        self.assertIn("__enable_irq();", early)
        self.assertNotIn("__enable_irq", (ROOT / "fw/main.c").read_text())

    def test_t5ai_leaves_cp_owned_watchdog_disabled(self):
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        early_init = board.split("void board_early_init(void)", 1)[1]
        early_init = early_init.split("void board_init(void)", 1)[0]
        self.assertIn("watchdog_init();", early_init)
        self.assertNotIn("watchdog_start();", early_init)

    def test_t5ai_kernelbg_uses_native_poll_and_message_queue(self):
        service = (ROOT / "fw/services/system_task/service.c").read_text()
        self.assertNotIn("CONFIG_SOC_T5AI", service)
        self.assertIn("pbl_poll_group_wait(&s_system_task_queue_set, PBL_FOREVER)", service)
        self.assertIn("pbl_msgq_get(activated_queue, &event, PBL_NO_WAIT)", service)
        self.assertIn("system_task_watchdog_feed();", service)
        app_manager = (ROOT / "fw/process_management/app_manager.c").read_text()
        self.assertNotIn("CONFIG_SOC_T5AI", app_manager)

    def test_t5ai_initializes_native_button_debouncing(self):
        main = (ROOT / "fw/main.c").read_text()
        build = (ROOT / "drivers/t5ai/CMakeLists.txt").read_text()
        self.assertIn("debounced_button_init();", main)
        self.assertIn("debounced_button.c", build)
        buttons = (ROOT / "drivers/t5ai/debounced_button.c").read_text()
        self.assertIn("pbl_thread_create", buttons)
        self.assertIn("pbl_sem_give", buttons)
        self.assertNotIn("system_task_add_callback", buttons)

    def test_t5ai_i2c_and_external_rtc(self):
        rtc = (ROOT / "drivers/rtc/t5ai.c").read_text()
        i2c = (ROOT / "drivers/i2c/t5ai.c").read_text()
        self.assertIn("T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE |= T5AI_I2C0_INTERRUPT_ENABLE", i2c)
        self.assertNotIn("T5AI_SYSTEM_CPU0_INTERRUPT_ENABLE", i2c)
        enable = i2c.split("static void prv_enable", 1)[1].split(
            "static void prv_disable", 1
        )[0]
        self.assertIn("delay_us(T5AI_I2C_ENABLE_SETTLE_US);", enable)
        start = i2c.split("static void prv_start", 1)[1].split(
            "void pbl_i2c_t5ai_irq_handler", 1
        )[0]
        self.assertIn("delay_us(T5AI_I2C_BUS_FREE_US);", start)
        disable = i2c.split("static void prv_disable", 1)[1].split(
            "static bool prv_is_busy", 1
        )[0]
        self.assertNotIn("prv_abort", disable)
        self.assertIn("~T5AI_I2C_CONFIG_ENABLE", disable)
        self.assertIn("NVIC_SetPriority(prv_dev(bus)->irqn, T5AI_I2C_IRQ_PRIORITY)", i2c)
        common_i2c = (ROOT / "drivers/i2c/i2c.c").read_text()
        self.assertRegex(common_i2c, r"#define I2C_ERROR_TIMEOUT_MS\s+\(1000\)")
        self.assertFalse((ROOT / "drivers/i2c/t5ai_bus.c").exists())
        self.assertIn("pbl_mutex_lock(&bus->state->mutex, PBL_FOREVER)", common_i2c)
        self.assertIn("if (!prv_external_rtc_read_raw(&control, registers))", rtc)
        self.assertIn("prv_set_retained_time(external_time);", rtc)
        self.assertIn("prv_external_rtc_write(fallback_time)", rtc)
        self.assertIn("T5AI_RTC_MIN_VALID_TIME", rtc)
        self.assertIn("#define T5AI_RTC_MIN_VALID_TIME 1577836800u", rtc)
        self.assertIn("if (base_ticks > ticks", rtc)

    def test_fault_priorities_use_unshifted_native_kernel_priority(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        fault = (ROOT / "fw/kernel/fault_handling.c").read_text()
        function = fault[fault.index("void enable_fault_handlers(void)"):fault.index("typedef struct CrashInfo")]
        harness = r"""
#include <assert.h>
#include <stdint.h>
#define PBL_IRQ_PRIO_MAX_SYSCALL 0x80u
#define __NVIC_PRIO_BITS 4u
#define MemoryManagement_IRQn 0
#define BusFault_IRQn 1
#define UsageFault_IRQn 2
#define SCB_SHCSR_MEMFAULTENA_Msk 1u
#define SCB_SHCSR_BUSFAULTENA_Msk 2u
#define SCB_SHCSR_USGFAULTENA_Msk 4u
#define SCB_CCR_DIV_0_TRP_Msk 16u
static struct { uint32_t SHCSR, CCR; } scb;
#define SCB (&scb)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
static unsigned priorities[3], calls;
static void NVIC_SetPriority(unsigned irq, unsigned prio) {
  assert(irq < 3); priorities[irq] = prio; ++calls;
}
""" + function + r"""
int main(void) {
  scb.SHCSR = 0x100u; scb.CCR = 0xffu;
  enable_fault_handlers();
  assert(calls == 3);
  for (unsigned i = 0; i < 3; ++i) assert(priorities[i] == 8u);
  assert(scb.SHCSR == 0x107u && scb.CCR == 0xefu);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "fault_priority.c"
            binary = Path(directory) / "fault_priority"
            source.write_text(harness)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_t5ai_scales_raw_aon_ticks_at_the_rtc_boundary(self):
        interface = (ROOT / "include/pbl/drivers/rtc.h").read_text()
        implementation = (ROOT / "drivers/rtc/t5ai.c").read_text()
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
        system_build = (ROOT / "fw/system/CMakeLists.txt").read_text()
        soc_build = (ROOT / "soc/t5ai/CMakeLists.txt").read_text()
        shared_bootbits = (ROOT / "fw/system/bootbits.c").read_text()
        shared_reboot_reason = (ROOT / "fw/system/reboot_reason.c").read_text()
        t5ai_bootbits = (ROOT / "soc/t5ai/bootbits.c").read_text()
        t5ai_reboot_reason = (ROOT / "soc/t5ai/reboot_reason.c").read_text()

        self.assertNotIn("CONFIG_SOC_T5AI", shared_bootbits)
        self.assertNotIn("CONFIG_SOC_T5AI", shared_reboot_reason)
        self.assertIn("pbl_filter_sources(sources EXCLUDE bootbits.c reboot_reason.c)", system_build)
        self.assertIn("bootbits.c", soc_build)
        self.assertIn("reboot_reason.c", soc_build)
        self.assertIn("t5ai_retained_init();", t5ai_bootbits)
        self.assertIn("t5ai_retained_write(REBOOT_REASON_REGISTER_1", t5ai_reboot_reason)


if __name__ == "__main__":
    unittest.main()
