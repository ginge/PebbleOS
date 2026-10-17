# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.boards import available_boards, parse_board


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
            "fw/prj.conf",
            f"fw/prj_{variant}.conf",
        ]
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
        for revision in ("dvt", "dvt2"):
            with self.subTest(revision=revision):
                target = f"getafix@{revision}"
                self.assertIn(target, choices)
                board = parse_board(str(ROOT), target)
                self.assertEqual(board.revision, revision)
                self.assertEqual(board.normalized, f"getafix_{revision}")
                expected_runners = ["sftool"]
                self.assertEqual(board.runners, expected_runners)

    def test_original_dvt_keeps_its_haptics(self):
        config = load_config("dvt", "normal")
        self.assertEqual(config["SHELL"], "y")
        self.assertEqual(config["VIBE_AW8623X"], "y")
        self.assertEqual(config["VIBE_AW86225"], "n")
        self.assertEqual(config["DISPLAY_FORCE_FULL_FRAME_UPDATES"], "y")


if __name__ == "__main__":
    unittest.main()
