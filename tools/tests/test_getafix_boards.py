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

    def test_best_effort_transport_validates_header_and_declared_length(self):
        best_effort = (
            ROOT / "src/fw/console/best_effort_transport.c"
        ).read_text()
        handler = best_effort.index(
            "void pulse2_best_effort_transport_on_packet"
        )
        header_check = best_effort.index(
            "raw_packet == NULL || length < sizeof(BestEffortPacket)",
            handler,
        )
        packet_cast = best_effort.index(
            "BestEffortPacket *packet = raw_packet", header_check
        )
        declaration = best_effort.index(
            "const uint16_t declared_length = ntoh16(packet->length)",
            packet_cast,
        )
        lower_bound = best_effort.index(
            "declared_length < sizeof(BestEffortPacket)", declaration
        )
        upper_bound = best_effort.index(
            "declared_length > length", lower_bound
        )
        payload_subtract = best_effort.index(
            "declared_length - sizeof(BestEffortPacket)", upper_bound
        )
        dispatch = best_effort.index(
            "switch (ntoh16(packet->protocol))", payload_subtract
        )
        self.assertLess(header_check, packet_cast)
        self.assertLess(packet_cast, declaration)
        self.assertLess(declaration, lower_bound)
        self.assertLess(lower_bound, upper_bound)
        self.assertLess(upper_bound, payload_subtract)
        self.assertLess(payload_subtract, dispatch)

    def test_pulse_control_replies_handle_link_teardown(self):
        control = (
            ROOT / "src/fw/console/pulse_control_message_protocol.c"
        ).read_text()
        handler = control.index(
            "void pulse_control_message_protocol_on_packet"
        )
        port_closed = control.index(
            "void pulse_control_message_protocol_send_port_closed_message",
            handler,
        )
        self.assertIn(
            "raw_packet == NULL || packet_length < sizeof(PCMPPacket)",
            control[handler:port_closed],
        )
        self.assertEqual(
            control.count("if (reply == NULL)"),
            control.count("PCMPPacket *reply = this->send_begin_fn"),
        )
        message_begin = control.index(
            "PCMPPacket *message = this->send_begin_fn", port_closed
        )
        message_check = control.index(
            "if (message == NULL)", message_begin
        )
        message_write = control.index(
            "message->code = PCMPCode_PortClosed", message_check
        )
        self.assertLess(message_begin, message_check)
        self.assertLess(message_check, message_write)

    def test_ppp_control_frames_are_bounded_and_reject_length_is_set(self):
        control = (ROOT / "src/fw/console/control_protocol.c").read_text()
        reject = control.index("static void prv_send_code_reject")
        reject_length = control.index(
            "packet->length = hton16(LCP_HEADER_LEN + body_len)", reject
        )
        reject_copy = control.index("memcpy(packet->data", reject_length)
        self.assertLess(reject_length, reject_copy)

        handler = control.index(
            "void ppp_control_protocol_handle_incoming_packet"
        )
        null_check = control.index(
            "raw_packet == NULL || length < sizeof(LCPPacket)", handler
        )
        packet_cast = control.index(
            "struct LCPPacket *packet = raw_packet", null_check
        )
        declaration = control.index(
            "const uint16_t declared_length = ntoh16(packet->length)",
            packet_cast,
        )
        lower_bound = control.index(
            "declared_length < sizeof(*packet)", declaration
        )
        upper_bound = control.index("declared_length > length", lower_bound)
        dispatch = control.index("switch (packet->code)", upper_bound)
        self.assertLess(null_check, packet_cast)
        self.assertLess(packet_cast, declaration)
        self.assertLess(declaration, lower_bound)
        self.assertLess(lower_bound, upper_bound)
        self.assertLess(upper_bound, dispatch)

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

    def test_bulkio_handles_reliable_link_teardown(self):
        bulkio = (ROOT / "src/fw/console/pulse_bulkio.c").read_text()
        self.assertEqual(
            bulkio.count("if (resp == NULL)"),
            bulkio.count("pulse_reliable_send_begin("),
        )
        self.assertIn("pulse_fd->read_active = false", bulkio)
        self.assertIn("state->impl->close_proc(state->domain_state)", bulkio)

        reliable = (ROOT / "src/fw/console/reliable_transport.c").read_text()
        send = reliable.index("void pulse_reliable_send(")
        assert_buffer = reliable.index("prv_assert_reliable_buffer(buf)", send)
        layer_down = reliable.index("if (!s_layer_up)", assert_buffer)
        release = reliable.index("xSemaphoreGive(s_tx_lock)", layer_down)
        send_packet = reliable.index("prv_send_info_packet(", release)
        self.assertLess(assert_buffer, layer_down)
        self.assertLess(layer_down, release)
        self.assertLess(release, send_packet)

    def test_pulse_pp_rejects_truncated_packets_and_link_teardown(self):
        pulse_pp = (ROOT / "src/fw/console/pulse_pp.c").read_text()
        send_cb = pulse_pp.index("static void prv_pulse_pp_send_cb")
        receive = pulse_pp.index(
            "void pulse_pp_transport_handle_received_data", send_cb
        )
        null_check = pulse_pp.index("if (resp)", send_cb, receive)
        copy = pulse_pp.index("memcpy(resp", send_cb, receive)
        free = pulse_pp.index("kernel_free(data)", send_cb, receive)
        self.assertLess(null_check, copy)
        self.assertLess(copy, free)

        header_check = pulse_pp.index(
            "data == NULL || length < sizeof(PulsePPPacket)", receive
        )
        opcode_read = pulse_pp.index("switch (packet->opcode)", receive)
        payload_length = pulse_pp.index(
            "length - sizeof(PulsePPPacket)", opcode_read
        )
        self.assertLess(header_check, opcode_read)
        self.assertLess(opcode_read, payload_length)

    def test_pulse_pp_revalidates_session_after_unlock(self):
        pulse_pp = (ROOT / "src/fw/console/pulse_pp.c").read_text()
        send_next = pulse_pp.index("static void prv_send_next")
        reset = pulse_pp.index("static void prv_reset", send_next)
        unlock = pulse_pp.index("bt_unlock()", send_next, reset)
        begin = pulse_pp.index("pulse_reliable_send_begin", unlock, reset)
        relock = pulse_pp.index("bt_lock()", begin, reset)
        generation_check = pulse_pp.index(
            "s_transport.session_generation != session_generation",
            relock,
            reset,
        )
        cancel = pulse_pp.index(
            "pulse_reliable_send_cancel(resp)", generation_check, reset
        )
        queue_copy = pulse_pp.index(
            "comm_session_send_queue_copy", cancel, reset
        )
        self.assertLess(unlock, begin)
        self.assertLess(begin, relock)
        self.assertLess(relock, generation_check)
        self.assertLess(generation_check, cancel)
        self.assertLess(cancel, queue_copy)

        open_failed = pulse_pp.index('PBL_LOG_ERR("CommSession couldn\'t be opened")')
        failed_return = pulse_pp.index("return;", open_failed)
        capabilities = pulse_pp.index(
            "comm_session_set_capabilities", failed_return
        )
        self.assertLess(failed_return, capabilities)
        self.assertGreaterEqual(
            pulse_pp.count("s_transport.session_generation++"), 2
        )

    def test_pulse_prompt_commands_are_bounded(self):
        prompt = (ROOT / "src/fw/console/prompt.c").read_text()

        reliable = prompt.index("void pulse2_prompt_packet_handler")
        legacy = prompt.index("void pulse_prompt_handler", reliable)
        reliable_check = prompt.index(
            "packet == NULL || length >= PROMPT_BUFFER_SIZE_BYTES",
            reliable,
            legacy,
        )
        reliable_copy = prompt.index(
            "memcpy(s_dbgserial_prompt_context.buffer", reliable_check, legacy
        )
        self.assertLess(reliable_check, reliable_copy)

        legacy_check = prompt.index(
            "packet == NULL || length < sizeof(PromptCommand)", legacy
        )
        legacy_bound = prompt.index(
            "length - sizeof(PromptCommand) >= PROMPT_BUFFER_SIZE_BYTES",
            legacy_check,
        )
        cookie_read = prompt.index("command->cookie", legacy_bound)
        command_length = prompt.index(
            "size_t command_length = length - sizeof(PromptCommand)",
            cookie_read,
        )
        legacy_copy = prompt.index(
            "strncpy(s_dbgserial_prompt_context.buffer",
            command_length,
        )
        self.assertLess(legacy_check, legacy_bound)
        self.assertLess(legacy_bound, cookie_read)
        self.assertLess(cookie_read, command_length)
        self.assertLess(command_length, legacy_copy)

    def test_pulse_prompt_responses_fit_transport_buffer(self):
        prompt = (ROOT / "src/fw/console/prompt.c").read_text()
        send = prompt.rindex("static void pulse_send_message")
        done = prompt.index("static void prv_pulse_done_command", send)
        reliable_limit = prompt.index(
            "pulse_reliable_max_send_size()", send, done
        )
        legacy_limit = prompt.index("PULSE_MAX_SEND_SIZE", reliable_limit, done)
        message_limit = prompt.index(
            "max_response_size - sizeof(PromptResponseContents)",
            legacy_limit,
            done,
        )
        bounded_length = prompt.index(
            "strnlen(response, max_message_size)", message_limit, done
        )
        begin = prompt.index("pulse_reliable_send_begin", bounded_length, done)
        copy = prompt.index("memcpy(contents->message", begin, done)
        self.assertLess(reliable_limit, legacy_limit)
        self.assertLess(legacy_limit, message_limit)
        self.assertLess(message_limit, bounded_length)
        self.assertLess(bounded_length, begin)
        self.assertLess(begin, copy)

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
        self.assertIn("T5AI_GPIO_BASE + 19u * sizeof(uint32_t)", board_source)
        self.assertIn(
            "*reg = value | T5AI_GPIO_OUTPUT | T5AI_GPIO_DRIVE_STRENGTH_1",
            board_source,
        )
        self.assertIn("prv_assert_power_latch();", board_source)
        self.assertIn(".pin = 28u", board_source)
        self.assertIn("gpio_output_set(&s_amplifier_enable, false)", board_source)

        imu_source = (
            ROOT / "src/fw/drivers/imu/qmi8658/qmi8658_t5ai.c"
        ).read_text()
        self.assertIn("#define QMI8658_RESET 0x60u", imu_source)
        self.assertIn("#define QMI8658_RESET_COMMAND 0xb0u", imu_source)
        reset = imu_source.index(
            "prv_write(QMI8658_RESET, QMI8658_RESET_COMMAND)"
        )
        identity = imu_source.index("prv_read(QMI8658_WHO_AM_I", reset)
        self.assertLess(reset, identity)
        status = imu_source.index("prv_read(QMI8658_STATUS0")
        sample = imu_source.index("prv_read(QMI8658_AX_L", status)
        self.assertLess(status, sample)
        self.assertIn("sample->x = sensor_y;", imu_source)
        self.assertIn("sample->y = -sensor_x;", imu_source)
        self.assertIn("sample->z = prv_to_mg(&raw[4]);", imu_source)

    def test_original_dvt_keeps_its_haptics(self):
        config = load_config("dvt", "normal")
        self.assertEqual(config["PROMPT"], "y")
        self.assertEqual(config["VIBE_AW8623X"], "y")
        self.assertEqual(config["VIBE_AW86225"], "n")


if __name__ == "__main__":
    unittest.main()
