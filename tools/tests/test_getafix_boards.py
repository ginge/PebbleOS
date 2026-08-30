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

    def test_original_dvt_keeps_its_haptics(self):
        config = load_config("dvt", "normal")
        self.assertEqual(config["VIBE_AW8623X"], "y")
        self.assertEqual(config["VIBE_AW86225"], "n")


if __name__ == "__main__":
    unittest.main()
