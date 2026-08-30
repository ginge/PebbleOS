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
        self.assertEqual(config["VIBE_AW8623X"], "y")
        self.assertEqual(config["VIBE_AW86225"], "n")


if __name__ == "__main__":
    unittest.main()
