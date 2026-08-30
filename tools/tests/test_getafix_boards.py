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
