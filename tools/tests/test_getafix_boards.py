# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.waf.boards import available_boards, parse_board


def load_config(revision, variant):
    with patch.dict(
        os.environ,
        {
            "srctree": str(ROOT),
            "BOARD": "getafix",
            "BOARD_REVISION": revision,
        },
    ):
        config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn=False)
        paths = [
            "boards/getafix/defconfig",
            f"boards/getafix/defconfig.{revision}",
            "src/fw/prj.conf",
            f"src/fw/prj_{variant}.conf",
        ]
        if revision == "dvt3":
            paths = paths[1:]
        for index, path in enumerate(paths):
            config.load_config(str(ROOT / path), replace=index == 0)
        return {
            name: symbol.str_value
            for name, symbol in config.syms.items()
            if symbol.nodes
        }


class GetafixBoardsTest(unittest.TestCase):
    def test_revision_discovery(self):
        choices = available_boards(str(ROOT))
        for revision in ("dvt", "dvt2", "dvt3"):
            with self.subTest(revision=revision):
                target = f"getafix@{revision}"
                self.assertIn(target, choices)
                board = parse_board(str(ROOT), target)
                self.assertEqual(board.revision, revision)
                self.assertEqual(board.normalized, f"getafix_{revision}")
                self.assertEqual(board.isolated_defconfig, revision == "dvt3")
                expected_runners = [] if revision == "dvt3" else ["sftool"]
                self.assertEqual(board.runners, expected_runners)
                self.assertEqual(board.allow_no_runner, revision == "dvt3")

    def test_dvt3_device_programming_is_fail_closed(self):
        dvt3 = parse_board(str(ROOT), "getafix@dvt3")
        self.assertEqual(dvt3.runners, [])

        cli = (ROOT / "pbl").read_text()
        self.assertIn("if env.CONFIG_SOC_T5AI:", cli)
        self.assertIn("no validated image container or recovery", cli)
        self.assertIn('_reject_unvalidated_t5ai_operation(env, "flash")', cli)
        self.assertIn(
            '_reject_unvalidated_t5ai_operation(env, "recovery imaging")', cli
        )

    def test_dvt3_internal_update_paths_are_fail_closed(self):
        build_script = (ROOT / "wscript").read_text()
        self.assertIn("T5AI recovery firmware is disabled", build_script)
        self.assertIn("T5AI bundles are disabled", build_script)

        firmware_update = (
            ROOT / "src/fw/services/firmware_update/service.c"
        ).read_text()
        self.assertIn("#ifdef CONFIG_SOC_T5AI", firmware_update)
        self.assertIn("return FirmwareUpdateCancelled;", firmware_update)

        put_bytes = (ROOT / "src/fw/services/put_bytes/put_bytes.c").read_text()
        t5ai_gate = put_bytes.index("Firmware object type %u is unavailable on T5AI")
        for object_name in ("ObjectFirmware", "ObjectRecovery", "ObjectSysResources"):
            self.assertIn(object_name, put_bytes[t5ai_gate - 250 : t5ai_gate])

        prf_update = (ROOT / "src/fw/services/prf_update/service.c").read_text()
        self.assertIn("Ignoring unsupported T5AI recovery firmware update", prf_update)

    def test_dvt3_raw_flash_console_paths_are_not_linked(self):
        console_build = (ROOT / "src/fw/console/wscript_build").read_text()
        self.assertIn("if bld.env.CONFIG_SOC_T5AI:", console_build)
        self.assertIn("'pulse_flash_imaging.c'", console_build)
        self.assertIn("'pulse_bulkio_external_flash.c'", console_build)
        self.assertIn("'pulse_bulkio_memory.c'", console_build)

        protocol_registry = (
            ROOT / "src/fw/console/pulse_protocol_registry.def"
        ).read_text()
        bulkio_registry = (
            ROOT / "src/fw/console/pulse_bulkio_handler.def"
        ).read_text()
        for registry, handler in (
            (protocol_registry, "pulse_flash_imaging_handler"),
            (bulkio_registry, "pulse_bulkio_domain_external_flash"),
            (bulkio_registry, "pulse_bulkio_domain_memory"),
        ):
            guard = registry.index("#if !defined(CONFIG_SOC_T5AI)")
            registration = registry.index(handler)
            end = registry.index("#endif", guard)
            self.assertLess(guard, registration)
            self.assertLess(registration, end)

        prompt_registration = protocol_registry.index(
            "REGISTER_PROTOCOL(PULSE_PROTOCOL_PROMPT"
        )
        prompt_guard = protocol_registry.rfind(
            "#ifdef CONFIG_PROMPT", 0, prompt_registration
        )
        prompt_end = protocol_registry.index("#endif", prompt_registration)
        self.assertGreaterEqual(prompt_guard, 0)
        self.assertLess(prompt_registration, prompt_end)

    def test_pfs_bulkio_validates_packet_bounds(self):
        pfs_bulkio = (ROOT / "src/fw/console/pulse_bulkio_pfs.c").read_text()
        self.assertIn(
            "packet_data == NULL || length <= sizeof(PFSOpenOptions)",
            pfs_bulkio,
        )
        self.assertIn(
            "resp == NULL || resp_max_len < sizeof(PFSStatResp)", pfs_bulkio
        )
        self.assertIn("packet_data == NULL || length == 0", pfs_bulkio)
        self.assertNotIn(
            "strncpy(filename, (char*)packet_data, FILE_MAX_NAME_LEN)",
            pfs_bulkio,
        )
        self.assertEqual(pfs_bulkio.count("if (address > INT_MAX)"), 2)
        self.assertEqual(
            pfs_bulkio.count("status = pfs_seek(fd, (int)address, FSeekSet)"),
            2,
        )
        self.assertEqual(pfs_bulkio.count("if (FAILED(status))"), 2)

    def test_bulkio_dispatcher_validates_frames_and_callback_lengths(self):
        bulkio = (ROOT / "src/fw/console/pulse_bulkio.c").read_text()
        for command in ("open.data", "write.data", "erase.data"):
            self.assertIn(f"offsetof(Command, {command})", bulkio)
        for command in ("close", "read", "crc", "stat"):
            self.assertIn(f"sizeof(cmd->{command})", bulkio)
        self.assertIn("length < minimum_length", bulkio)
        self.assertIn('"Truncated command"', bulkio)
        self.assertIn("(size_t)ret <= read_len", bulkio)
        self.assertIn("(uint32_t)ret > read_len", bulkio)
        self.assertIn("crc32(crc, buffer, ret)", bulkio)
        self.assertEqual(
            bulkio.count("!prv_u32_range_valid(cmd->"), 3
        )
        self.assertIn("(size_t)ret > payload_length", bulkio)
        self.assertIn(".length = ret", bulkio)
        self.assertIn("(size_t)ret <= data_max_len", bulkio)
        self.assertIn("!s_transfer_fds[i].read_callback_pending", bulkio)
        self.assertIn("pulse_fd->read_callback_pending = false", bulkio)
        self.assertIn("pulse_fd->impl == NULL || !pulse_fd->read_active", bulkio)
        self.assertIn("if (pulse_fd->read_active)", bulkio)
        self.assertIn("prv_respond_internal_error(cmd, length, E_BUSY)", bulkio)

        close_call = bulkio.index(
            "pulse_fd->impl->close_proc(pulse_fd->domain_state)"
        )
        close_free = bulkio.index("prv_free_fd(cmd->close.fd)", close_call)
        close_failure = bulkio.index("if (FAILED(status))", close_call)
        self.assertLess(close_free, close_failure)

        fd_check = bulkio.index("if (fd >= 0 && fd < MAX_PULSE_FDS")
        fd_index = bulkio.index("&s_transfer_fds[fd]", fd_check)
        self.assertLess(fd_check, fd_index)

    def test_reliable_transport_validates_declared_length_before_dispatch(self):
        reliable = (ROOT / "src/fw/console/reliable_transport.c").read_text()
        declaration = reliable.index(
            "const uint16_t declared_length = ntoh16(packet->i.length)"
        )
        lower_bound = reliable.index(
            "declared_length >= sizeof(ReliablePacket)", declaration
        )
        upper_bound = reliable.index("declared_length <= length", lower_bound)
        sequence_advance = reliable.index(
            "s_receive_variable = (s_receive_variable + 1) % MODULUS",
            upper_bound,
        )
        payload_subtract = reliable.index(
            "declared_length - sizeof(ReliablePacket)", sequence_advance
        )
        self.assertLess(declaration, lower_bound)
        self.assertLess(lower_bound, upper_bound)
        self.assertLess(upper_bound, sequence_advance)
        self.assertLess(sequence_advance, payload_subtract)

    def test_bulkio_responses_use_reliable_payload_limit(self):
        bulkio = (ROOT / "src/fw/console/pulse_bulkio.c").read_text()
        self.assertEqual(bulkio.count("pulse_reliable_max_send_size()"), 4)
        self.assertNotIn(
            "PULSE_MAX_SEND_SIZE - sizeof(ReadResponse)", bulkio
        )
        self.assertNotIn(
            "PULSE_MAX_SEND_SIZE - sizeof(StatResponse)", bulkio
        )
        self.assertNotIn(
            "PULSE_MAX_SEND_SIZE - response_len", bulkio
        )

    def test_bulkio_packet_handoff_is_bounded(self):
        bulkio = (ROOT / "src/fw/console/pulse_bulkio.c").read_text()
        self.assertIn("packet == NULL && length != 0", bulkio)
        self.assertIn("length > PULSE_MAX_RECEIVE_UNIT", bulkio)
        self.assertIn(
            "length > SIZE_MAX - sizeof(BulkIOPacketCallbackData)", bulkio
        )
        self.assertIn("kernel_malloc_check(sizeof(*data) + length)", bulkio)
        self.assertIn("if (length != 0)", bulkio)
        self.assertIn("memcpy(data->packet, packet, length)", bulkio)

    def test_framebuffer_bulkio_is_bounded(self):
        framebuffer = (
            ROOT / "src/fw/console/pulse_bulkio_framebuffer.c"
        ).read_text()
        self.assertIn("length > 0 && length <= FRAMEBUFFER_SIZE_BYTES", framebuffer)
        self.assertIn(
            "address <= FRAMEBUFFER_SIZE_BYTES - length", framebuffer
        )
        self.assertEqual(
            framebuffer.count("!prv_framebuffer_range_valid(address, length)"),
            2,
        )
        self.assertIn(
            "resp == NULL || resp_max_len < sizeof(FramebufferStatResp)",
            framebuffer,
        )

    def test_coredump_bulkio_is_bounded(self):
        coredump = (
            ROOT / "src/fw/console/pulse_bulkio_coredump.c"
        ).read_text()
        self.assertIn("length != sizeof(*index)", coredump)
        self.assertIn("memcpy(index, packet_data, sizeof(*index))", coredump)
        self.assertIn("*index < CORE_DUMP_MAX_IMAGES", coredump)
        self.assertIn("length > 0 && length <= data_size", coredump)
        self.assertIn("address <= data_size - length", coredump)
        self.assertIn(
            "resp == NULL || resp_max_len < sizeof(CoredumpStatResp)",
            coredump,
        )
        self.assertEqual(
            coredump.count("prv_get_coredump_index(packet_data, length, &index)"),
            2,
        )
        self.assertNotIn("*(uint32_t*)packet_data", coredump)

    def test_dvt3_persistent_flash_logging_is_not_linked(self):
        debug_build = (ROOT / "src/fw/debug/wscript_build").read_text()
        self.assertIn("if bld.env.CONFIG_SOC_T5AI:", debug_build)
        self.assertIn("excl = ['flash_logging.c']", debug_build)
        self.assertIn("flash_logging_unavailable.c", debug_build)

        unavailable = (
            ROOT / "src/fw/debug/flash_logging_unavailable.c"
        ).read_text()
        self.assertIn("return FLASH_LOG_INVALID_ADDR;", unavailable)
        self.assertIn("completed_cb(false);", unavailable)

    def test_dvt3_factory_reset_does_not_request_prf(self):
        factory_reset = (
            ROOT / "src/fw/kernel/util/factory_reset.c"
        ).read_text()
        non_pfs_wipe = factory_reset.index("shared_prf_storage_wipe_all();")
        non_pfs_guard = factory_reset.rfind(
            "#ifndef CONFIG_SOC_T5AI", 0, non_pfs_wipe
        )
        non_pfs_end = factory_reset.index("#endif", non_pfs_wipe)
        self.assertGreaterEqual(non_pfs_guard, 0)
        self.assertLess(non_pfs_wipe, non_pfs_end)

        reset_start = factory_reset.index("void factory_reset(bool should_shutdown)")
        format_call = factory_reset.index("pfs_format(", reset_start)
        prf_bit = factory_reset.index("boot_bit_set(BOOT_BIT_FORCE_PRF)", reset_start)
        prf_guard = factory_reset.rfind(
            "#ifndef CONFIG_SOC_T5AI", reset_start, prf_bit
        )
        prf_end = factory_reset.index("#endif // !CONFIG_SOC_T5AI", prf_bit)
        self.assertGreaterEqual(prf_guard, reset_start)
        self.assertLess(prf_bit, prf_end)
        self.assertLess(prf_end, format_call)

        consumer_mode = factory_reset.index("void command_enter_consumer_mode")
        consumer_bit = factory_reset.index(
            "boot_bit_set(BOOT_BIT_FORCE_PRF)", consumer_mode
        )
        consumer_guard = factory_reset.rfind(
            "#ifndef CONFIG_SOC_T5AI", consumer_mode, consumer_bit
        )
        consumer_end = factory_reset.index("#endif", consumer_bit)
        self.assertGreaterEqual(consumer_guard, consumer_mode)
        self.assertLess(consumer_bit, consumer_end)

    def test_dvt3_shared_prf_storage_is_read_only(self):
        shared_prf = (
            ROOT / "src/fw/services/shared_prf_storage/shared_prf_storage.c"
        ).read_text()
        write_helper = shared_prf.index("static void prv_flash_write(")
        t5ai_guard = shared_prf.index("#ifdef CONFIG_SOC_T5AI", write_helper)
        real_write = shared_prf.index("flash_write_bytes(", t5ai_guard)
        guard_else = shared_prf.index("#else", t5ai_guard)
        guard_end = shared_prf.index("#endif", guard_else)
        self.assertLess(t5ai_guard, guard_else)
        self.assertLess(guard_else, real_write)
        self.assertLess(real_write, guard_end)
        self.assertEqual(shared_prf.count("flash_write_bytes("), 1)

        erase_helper = shared_prf.index("static void prv_flash_erase_region(")
        erase_guard = shared_prf.index("#ifndef CONFIG_SOC_T5AI", erase_helper)
        real_erase = shared_prf.index(
            "flash_region_erase_optimal_range_no_watchdog(", erase_guard
        )
        erase_end = shared_prf.index("#endif", real_erase)
        self.assertLess(erase_guard, real_erase)
        self.assertLess(real_erase, erase_end)
        self.assertEqual(
            shared_prf.count("flash_region_erase_optimal_range_no_watchdog("), 1
        )

    def test_dvt3_persistent_coredumps_are_disabled(self):
        core_dump = (ROOT / "src/fw/kernel/core_dump.c").read_text()

        reset = core_dump.index("NORETURN core_dump_reset(bool is_forced)")
        reset_guard = core_dump.index("#ifdef CONFIG_SOC_T5AI", reset)
        unavailable = core_dump.index(
            'prv_debug_str("Persistent core dumps unavailable on T5AI")',
            reset_guard,
        )
        immediate_reset = core_dump.index("prv_reset();", unavailable)
        reset_else = core_dump.index("#else", immediate_reset)
        self.assertLess(reset_guard, unavailable)
        self.assertLess(immediate_reset, reset_else)

        reserve = core_dump.index("bool core_dump_reserve_ble_slot(")
        reserve_guard = core_dump.index("#ifdef CONFIG_SOC_T5AI", reserve)
        reserve_failure = core_dump.index("return false;", reserve_guard)
        reserve_else = core_dump.index("#else", reserve_failure)
        self.assertLess(reserve_guard, reserve_failure)
        self.assertLess(reserve_failure, reserve_else)

        mark_read = core_dump.index("void core_dump_mark_read(")
        mark_guard = core_dump.index("#ifdef CONFIG_SOC_T5AI", mark_read)
        mark_else = core_dump.index("#else", mark_guard)
        mark_write = core_dump.index("flash_write_bytes(", mark_else)
        mark_end = core_dump.index("#endif", mark_write)
        self.assertLess(mark_guard, mark_else)
        self.assertLess(mark_else, mark_write)
        self.assertLess(mark_write, mark_end)

    def test_dvt3_has_native_t5ai_identity(self):
        dvt3 = load_config("dvt3", "normal")
        self.assertEqual(dvt3["BOARD_GETAFIX_DVT3"], "y")
        self.assertEqual(dvt3["SOC_T5AI"], "y")
        self.assertEqual(dvt3["CORTEX_M33"], "y")
        self.assertEqual(dvt3["PLATFORM_GABBRO"], "y")
        self.assertEqual(dvt3["VIBE"], "n")
        self.assertEqual(dvt3["PMIC"], "n")
        self.assertEqual(dvt3["MAG"], "n")
        self.assertEqual(dvt3["ACCEL_QMI8658_T5AI"], "y")
        self.assertEqual(dvt3["ACCEL_LIS2DW12"], "n")
        self.assertEqual(dvt3["BATTERY_T5AI"], "y")
        self.assertEqual(dvt3["BATTERY_PMIC"], "n")
        self.assertEqual(dvt3["BT_FW_STUB"], "y")
        self.assertEqual(dvt3["BT_FW_NIMBLE"], "n")
        self.assertEqual(dvt3["T5AI_CP_HCI_VALIDATED"], "n")
        self.assertEqual(dvt3["PROMPT"], "n")
        self.assertEqual(dvt3["SPEAKER"], "n")
        self.assertEqual(dvt3["MIC"], "n")
        self.assertEqual(dvt3["VIBE"], "n")
        self.assertEqual(dvt3["AMBIENT_LIGHT"], "n")
        self.assertEqual(dvt3["PRESSURE"], "n")
        self.assertEqual(dvt3["TEMPERATURE"], "n")

        board_source = (ROOT / "src/fw/board/boards/board_t5ai.c").read_text()
        self.assertIn(".pin = 19u", board_source)
        self.assertIn("gpio_output_set(&s_power_latch, true)", board_source)
        self.assertIn(".pin = 28u", board_source)
        self.assertIn("gpio_output_set(&s_amplifier_enable, false)", board_source)

    def test_original_dvt_keeps_its_haptics(self):
        config = load_config("dvt", "normal")
        self.assertEqual(config["PROMPT"], "y")
        self.assertEqual(config["VIBE_AW8623X"], "y")
        self.assertEqual(config["VIBE_AW86225"], "n")


if __name__ == "__main__":
    unittest.main()
