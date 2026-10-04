# SPDX-License-Identifier: Apache-2.0

import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.waf.boards import available_boards, parse_board


def load_config(board, variant):
    with patch.dict(
        os.environ,
        {"srctree": str(ROOT), "BOARD": board, "BOARD_REVISION": "_"},
    ):
        config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn=False)
        paths = (
            f"boards/{board}/defconfig",
            "src/fw/prj.conf",
            f"src/fw/prj_{variant}.conf",
        )
        for index, path in enumerate(paths):
            config.load_config(str(ROOT / path), replace=index == 0)
        return {
            name: symbol.str_value
            for name, symbol in config.syms.items()
            if symbol.nodes
        }


class UnhygienixBoardsTest(unittest.TestCase):
    def test_unhygienix_pairing_uses_its_two_present_buttons(self):
        board_source = (ROOT / "src/fw/board/boards/board_unhygienix.c").read_text()
        self.assertIn('[BUTTON_ID_UP] = {.name = "Up", .pin = UINT8_MAX}', board_source)
        self.assertIn('[BUTTON_ID_DOWN] = {.name = "Down", .pin = UINT8_MAX}', board_source)

        pairing_ui = (ROOT / "src/fw/popups/bluetooth_pairing_ui.c").read_text()
        unhygienix_controls = pairing_ui[
            pairing_ui.index("#ifdef CONFIG_BOARD_UNHYGIENIX") :
            pairing_ui.index("#endif", pairing_ui.index("#ifdef CONFIG_BOARD_UNHYGIENIX"))
        ]
        self.assertIn(
            "#define BT_PAIRING_CONFIRM_BUTTON BUTTON_ID_SELECT", unhygienix_controls
        )
        self.assertIn(
            "#define BT_PAIRING_DECLINE_BUTTON BUTTON_ID_BACK", unhygienix_controls
        )

    def test_unhygienix_only_exposes_exact_range_flash_runner(self):
        unhygienix = parse_board(str(ROOT), "unhygienix")
        self.assertEqual(unhygienix.runners, ["tyutool"])

        cli = (ROOT / "pbl").read_text()
        self.assertIn("if env.CONFIG_SOC_T5AI:", cli)
        self.assertIn("no validated image container or recovery", cli)
        self.assertIn('if command != "flash":', cli)
        self.assertIn(
            '_reject_unvalidated_t5ai_operation(env, "recovery imaging")', cli
        )

        runner = (ROOT / "tools/runners/tyutool.py").read_text()
        self.assertIn('commands={"flash"}', runner)
        self.assertIn('("AP", PEBBLE_AP_PHYSICAL_OFFSET', runner)
        self.assertIn('("PFS", PEBBLE_FILESYSTEM_PHYSICAL_OFFSET', runner)
        self.assertIn('end = start + len(image)', runner)
        self.assertNotIn('"erase"', runner)

    def test_unhygienix_internal_update_paths_are_fail_closed(self):
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

    def test_unhygienix_reports_round_2_identity_without_enabling_updates(self):
        metadata = (ROOT / "include/pebbleos/firmware_metadata.h").read_text()
        unhygienix_identity = metadata[
            metadata.index("#elif defined(CONFIG_BOARD_UNHYGIENIX)") : metadata.index(
                "#elif defined(CONFIG_BOARD_QEMU_EMERY)"
            )
        ]
        self.assertIn(
            "FirmwareMetadataPlatformPebbleGetafixDVT2", unhygienix_identity
        )
        self.assertNotIn("FirmwareMetadataPlatformUnknown", unhygienix_identity)

        versions = (ROOT / "src/fw/kernel/system_versions.c").read_text()
        compatibility_start = versions.index(
            "#if defined(CONFIG_BOARD_UNHYGIENIX)"
        )
        compatibility = versions[
            compatibility_start : versions.index("#else", compatibility_start)
        ]
        self.assertIn("if (!has_recovery_fw)", compatibility)
        self.assertIn(
            "version_copy_running_fw_metadata(&versions_msg.recovery_fw_metadata)",
            compatibility,
        )
        self.assertIn(
            "versions_msg.recovery_fw_metadata.is_recovery_firmware = true",
            compatibility,
        )

        # The compatibility record is phone-facing only. Real PRF detection and
        # the fail-closed firmware transfer gates must remain in place.
        version = (ROOT / "src/fw/system/version.c").read_text()
        self.assertIn("bool version_is_prf_installed(void)", version)
        firmware_update = (
            ROOT / "src/fw/services/firmware_update/service.c"
        ).read_text()
        self.assertIn("Firmware update is unavailable on T5AI", firmware_update)
        put_bytes = (ROOT / "src/fw/services/put_bytes/put_bytes.c").read_text()
        rejection = put_bytes.index(
            "Firmware object type %u is unavailable on T5AI"
        )
        for object_name in ("ObjectFirmware", "ObjectRecovery", "ObjectSysResources"):
            self.assertIn(object_name, put_bytes[rejection - 250 : rejection])

    def test_unhygienix_initializes_the_provisioned_filesystem(self):
        services = (
            ROOT / "src/fw/services/services_normal/service.c"
        ).read_text()
        early_init = services[
            services.index("void services_normal_early_init(void)") :
            services.index("void services_normal_init(void)")
        ]
        self.assertIn("pfs_init(true);", early_init)
        self.assertNotIn("CONFIG_SOC_T5AI", early_init)

        main = (ROOT / "src/fw/main.c").read_text()
        flash_init = main.index("  flash_init();")
        flash_ready_wait = main.rindex(
            "vTaskDelay(pdMS_TO_TICKS(2000));", 0, flash_init
        )
        self.assertLess(flash_ready_wait, flash_init)

    def test_unhygienix_raw_flash_console_paths_are_not_linked(self):
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

    def test_unhygienix_persistent_flash_logging_is_not_linked(self):
        debug_build = (ROOT / "src/fw/debug/wscript_build").read_text()
        self.assertIn("if bld.env.CONFIG_SOC_T5AI:", debug_build)
        self.assertIn("excl = ['flash_logging.c']", debug_build)
        self.assertIn("flash_logging_unavailable.c", debug_build)

        unavailable = (
            ROOT / "src/fw/debug/flash_logging_unavailable.c"
        ).read_text()
        self.assertIn("return FLASH_LOG_INVALID_ADDR;", unavailable)
        self.assertIn("completed_cb(false);", unavailable)

    def test_unhygienix_factory_reset_does_not_request_prf(self):
        factory_reset = (
            ROOT / "src/fw/kernel/util/factory_reset.c"
        ).read_text()
        non_pfs_wipe = factory_reset.index("shared_prf_storage_wipe_all();")
        reset_start = factory_reset.index("void factory_reset(bool should_shutdown)")
        self.assertLess(non_pfs_wipe, reset_start)

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

    def test_unhygienix_shared_prf_storage_uses_reserved_writable_data_partition(self):
        shared_prf = (
            ROOT / "src/fw/services/shared_prf_storage/shared_prf_storage.c"
        ).read_text()
        regions = (ROOT / "src/fw/flash_region/flash_region_t5ai.h").read_text()
        flash = (ROOT / "src/fw/drivers/flash/t5ai.c").read_text()
        factory_reset = (
            ROOT / "src/fw/kernel/util/factory_reset.c"
        ).read_text()

        self.assertEqual(shared_prf.count("flash_write_bytes("), 1)
        self.assertEqual(
            shared_prf.count("flash_region_erase_optimal_range_no_watchdog("), 1
        )
        self.assertIn("FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x024ea000", regions)
        self.assertIn("FLASH_REGION_SHARED_PRF_STORAGE_END 0x024fa000", regions)
        self.assertIn("in_shared_prf", flash)
        self.assertIn("shared_prf_storage_wipe_all();", factory_reset)

    def test_unhygienix_persistent_coredumps_are_disabled(self):
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

    def test_unhygienix_has_native_t5ai_identity(self):
        unhygienix = load_config("unhygienix", "normal")
        self.assertEqual(unhygienix["BOARD_UNHYGIENIX"], "y")
        self.assertNotIn("BOARD_GETAFIX", unhygienix)
        self.assertEqual(unhygienix["SOC_T5AI"], "y")
        self.assertEqual(unhygienix["CORTEX_M33"], "y")
        self.assertEqual(unhygienix["PLATFORM_GABBRO"], "y")
        self.assertEqual(unhygienix["MODDABLE_XS"], "y")
        self.assertEqual(unhygienix["VIBE"], "n")
        self.assertEqual(unhygienix["PMIC"], "n")
        self.assertEqual(unhygienix["MAG"], "n")
        self.assertEqual(unhygienix["ACCEL_QMI8658_T5AI"], "y")
        self.assertEqual(unhygienix["ACCEL_LIS2DW12"], "n")
        self.assertEqual(unhygienix["BATTERY_T5AI"], "y")
        self.assertEqual(unhygienix["BATTERY_PMIC"], "n")
        self.assertEqual(unhygienix["BT_FW_STUB"], "n")
        self.assertEqual(unhygienix["BT_FW_NIMBLE"], "y")
        self.assertEqual(unhygienix["T5AI_CP_HCI_VALIDATED"], "y")
        self.assertEqual(unhygienix["PROMPT"], "n")
        self.assertEqual(unhygienix["SPEAKER"], "n")
        self.assertEqual(unhygienix["MIC"], "y")
        self.assertEqual(unhygienix["MIC_T5AI"], "y")
        self.assertEqual(unhygienix["SERVICE_VOICE"], "y")
        self.assertEqual(unhygienix["VIBE"], "n")
        self.assertEqual(unhygienix["AMBIENT_LIGHT"], "n")
        self.assertEqual(unhygienix["DISPLAY_FORCE_FULL_FRAME_UPDATES"], "n")
        self.assertEqual(unhygienix["PRESSURE"], "n")
        self.assertEqual(unhygienix["TEMPERATURE"], "n")

    def test_unhygienix_hci_callback_buffers_have_explicit_ownership(self):
        nimble_build = (ROOT / "third_party/nimble/wscript_build").read_text()
        self.assertIn(
            "'MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_HS_COUNT=10'", nimble_build
        )
        self.assertIn(
            "'MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT=32'", nimble_build
        )
        self.assertIn(
            "'MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_HS_COUNT=0'", nimble_build
        )

        source = (
            ROOT / "third_party/nimble/transport/t5ai_hci_ipc.c"
        ).read_text()
        snapshot = source.index("const uint8_t subtype = message->subtype")
        length_snapshot = source.index(
            "const size_t request_length = message->request_length", snapshot
        )
        copy = source.index(
            "memcpy(payload, (const void *)(uintptr_t)request, request_length)",
            length_snapshot,
        )
        response = source.index("status = prv_respond_to_event(message)", copy)
        callback = source.index("callback(payload, request_length)", response)
        self.assertLess(snapshot, length_snapshot)
        self.assertLess(length_snapshot, copy)
        self.assertLess(copy, response)
        self.assertLess(response, callback)
        callback_path = source[response:callback]
        self.assertNotIn("message->subtype", callback_path)
        self.assertNotIn("message->request_length", callback_path)
        self.assertIn("s_event_responses[T5AI_HCI_EVENT_RESPONSE_CAPACITY]", source)
        self.assertIn("prv_retire_event_response", source)
        self.assertIn("s_requests[T5AI_HCI_REQUEST_CAPACITY]", source)
        self.assertIn("static void prv_worker", source)
        self.assertIn("xSemaphoreGive(s_exchange_ready)", source)
        self.assertNotIn("s_request_transport_ack", source)
        self.assertNotIn("prv_wait_for_event_response_ack", source)

        mailbox = (ROOT / "src/fw/drivers/t5ai/mailbox_channel.c").read_text()
        self.assertIn("static TxChannel s_tx[T5AI_MAILBOX_CHANNEL_COUNT]", mailbox)
        self.assertNotIn("s_active_channel", mailbox)
        self.assertIn("sequence != tx->sequence", mailbox)
        self.assertIn("tx->active_start", mailbox)
        self.assertIn(
            "MailboxOwnedSlot s_ack_slots[T5AI_MAILBOX_CHANNEL_COUNT]", mailbox
        )
        self.assertIn("s_ack_slots[channel & T5AI_MAILBOX_CHANNEL_INDEX_MASK]", mailbox)
        command = mailbox[mailbox.index("static void prv_process_command"):]
        command = command[:command.index("static bool prv_drain_rx")]
        self.assertIn("s_pending_ack_mask |=", command)
        self.assertIn("if (prv_publish(ack) == E_BUSY)", command)
        self.assertNotIn("while (prv_publish", command)
        drain = mailbox[mailbox.index("static bool prv_drain_rx"):]
        drain = drain[:drain.index("static bool prv_publish_pending_acks")]
        self.assertIn("MAILBOX_RUNTIME_DRAIN_LIMIT", drain)
        self.assertIn("drained < MAILBOX_RUNTIME_DRAIN_LIMIT", drain)
        self.assertNotIn(
            "while ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u)", drain
        )
        mailbox_task = mailbox[mailbox.index("static void prv_mailbox_task"):]
        mailbox_task = mailbox_task[:mailbox_task.index("void MAILBOX_IRQHandler")]
        self.assertIn("const bool ack_pending = !prv_publish_pending_acks()", mailbox_task)
        self.assertIn(
            "(rx_pending || ack_pending || reset_pending) ? E_BUSY : prv_schedule_tx()",
            mailbox_task,
        )
        self.assertNotIn("(rx_pending || ack_pending) ? E_BUSY", mailbox_task)
        self.assertIn(
            "rx_pending || ack_pending || reset_pending || schedule_status == E_BUSY",
            mailbox_task,
        )
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(MAILBOX_BUSY_RETRY_MS))", mailbox_task)
        self.assertIn("dcache_invalidate((void *)address", mailbox)
        self.assertNotIn("dcache_flush((void *)address", mailbox)
        self.assertIn("prv_bootstrap_resynchronize(poll_ms)", mailbox)
        reset = mailbox[mailbox.index("static void prv_send_reset"):]
        reset = reset[:reset.index("static bool prv_bootstrap_publish")]
        self.assertIn("if (prv_publish(&s_reset_slot) == S_SUCCESS)", reset)
        self.assertNotIn("while (prv_publish(&s_reset_slot)", reset)
        bootstrap = mailbox[mailbox.index("static void prv_bootstrap_resynchronize"):]
        bootstrap = bootstrap[:bootstrap.index("status_t t5ai_mailbox_channel_init")]
        self.assertIn("prv_process_command(&message)", bootstrap)
        self.assertIn("MAILBOX_BOOTSTRAP_DRAIN_LIMIT", bootstrap)
        self.assertIn("drained < MAILBOX_BOOTSTRAP_DRAIN_LIMIT", bootstrap)
        self.assertIn("T5AI_AON_RTC_COUNT_LOW - start", bootstrap)
        self.assertNotIn(
            "while ((MBOX_CH1_CTRL & MBOX_FIFO_NOT_EMPTY) != 0u)", bootstrap
        )
        self.assertNotIn("MBOX_RESET", bootstrap)
        self.assertNotIn("prv_publish((const T5AIMailboxMessage *)(uintptr_t)", bootstrap)
        mipc_ack = source[source.index("static status_t prv_mipc_ack"):]
        mipc_ack = mipc_ack[:mipc_ack.index("static status_t prv_prepare_outbound")]
        self.assertIn("return prv_send_frame(&ack)", mipc_ack)

        transport = (
            ROOT / "third_party/nimble/transport/hci_t5ai.c"
        ).read_text()
        self.assertNotIn("vTaskDelay(pdMS_TO_TICKS(100))", transport)
        self.assertIn("T5AI_HCI_ACL_TX_QUEUE_CAPACITY 16u", transport)
        self.assertIn("xQueueSend(s_acl_tx_queue, &acl, 0)", transport)
        self.assertIn("prv_send_queued_acl(acl)", transport)
        self.assertNotIn("T5AI_HCI_ACL_SEND_ATTEMPTS", transport)

        flash_driver = (ROOT / "src/fw/drivers/flash/t5ai.c").read_text()
        blank_check = flash_driver.split("static status_t prv_blank_check", 1)[1]
        blank_check = blank_check.split("status_t flash_impl_blank_check_subsector", 1)[0]
        self.assertIn("if (FAILED(status))", blank_check)
        self.assertIn("return S_FALSE;", blank_check)

    def test_unhygienix_uses_normal_bluetooth_control_with_shared_prf_storage(self):
        main = (ROOT / "src/fw/main.c").read_text()
        self.assertIn("static void prv_t5ai_bluetooth_init(void)", main)
        self.assertIn("bt_persistent_storage_init();", main)
        self.assertIn("bt_ctl_init();", main)
        self.assertIn("bt_ctl_set_enabled(true);", main)
        self.assertNotIn("prv_t5ai_nimble_probe_start", main)
        self.assertNotIn("ble_gap_adv_start(", main)

        comm_build = (ROOT / "src/fw/comm/wscript_build").read_text()
        self.assertIn("excl.append('ble/gap_le_connect_params.c')", comm_build)
        t5_params = (
            ROOT / "src/fw/comm/ble/gap_le_connect_params_t5ai.c"
        ).read_text()
        self.assertIn("T5AI_FAST_INTERVAL_1_25MS 12u", t5_params)
        self.assertNotIn("T5AI_IDLE_INTERVAL_1_25MS", t5_params)
        self.assertNotIn("bt_driver_le_connection_parameter_update", t5_params)
        self.assertIn("conn_mgr_handle_desired_state_granted(connection, desired_state)", t5_params)
        self.assertIn("conn_mgr_handle_desired_state_granted(connection, desired_state)",
                      t5_params)
        self.assertIn("T5AI conn param result", t5_params)
        update_handler = t5_params.index("bt_driver_handle_le_conn_params_update_event")
        self.assertLess(
            t5_params.index("connection->param_update_info.is_request_pending = false;",
                            update_handler),
            t5_params.index("event->status == HciStatusCode_Success", update_handler),
        )
        self.assertNotIn("prv_watchdog", t5_params)
        self.assertIn("conn_mgr_handle_desired_state_granted", t5_params)

        app_cache = (ROOT / "src/fw/services/app_cache/service.c").read_text()
        self.assertIn("defined(CONFIG_SOC_T5AI)", app_cache)
        self.assertIn("#define APP_SPACE_BUFFER KiBYTES(300)", app_cache)

        build = (
            ROOT / "src/fw/services/bluetooth/wscript_build"
        ).read_text()
        self.assertIn("not bld.env.CONFIG_SOC_T5AI", build)
        self.assertIn(
            "sources.append('bluetooth_persistent_storage_prf.c')", build
        )

        shared_prf = (
            ROOT / "src/fw/services/shared_prf_storage/shared_prf_storage.c"
        ).read_text()
        self.assertIn("flash_write_bytes(data, address, length);", shared_prf)
        self.assertNotIn("keep PRF-backed Bluetooth state volatile", shared_prf)

        board_source = (ROOT / "src/fw/board/boards/board_unhygienix.c").read_text()
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
        self.assertIn("sample->z = prv_to_mg(&raw[QMI8658_ACCEL_Z_OFFSET]);", imu_source)

        battery_source = (ROOT / "src/fw/drivers/battery/t5ai.c").read_text()
        self.assertIn(
            "gpio_input_init_pull_up_down(&s_charge, GPIO_PuPd_UP)",
            battery_source,
        )

    def test_independent_board_discovery(self):
        choices = available_boards(str(ROOT))
        self.assertIn("unhygienix", choices)
        board = parse_board(str(ROOT), "unhygienix")
        self.assertEqual(board.normalized, "unhygienix")
        self.assertIsNone(board.revision)
        self.assertEqual(board.runners, ["tyutool"])
        with self.assertRaisesRegex(ValueError, "Unknown board"):
            parse_board(str(ROOT), "unknown-board")


if __name__ == "__main__":
    unittest.main()
