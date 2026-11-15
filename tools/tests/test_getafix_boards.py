# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
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
        config = kconfiglib.Kconfig(str(ROOT / "pebble/Kconfig"), warn=False)
        paths = [
            "boards/getafix/defconfig",
            "pebble/prj.conf",
            f"pebble/prj_{variant}.conf",
        ]
        for index, path in enumerate(paths):
            config.load_config(str(ROOT / path), replace=index == 0)
        return {
            name: symbol.str_value
            for name, symbol in config.syms.items()
            if symbol.nodes
        }


class GetafixBoardsTest(unittest.TestCase):
    def test_revision_haptics_device(self):
        for revision, model, address in (
            ("dvt", "aw86233", "5a"),
            ("dvt2", "aw86225", "58"),
        ):
            with self.subTest(revision=revision):
                dts = (ROOT / f"boards/getafix/getafix_{revision}.dts").read_text()
                self.assertIn(f"haptics: haptics@{address}", dts)
                self.assertIn(f'compatible = "awinic,{model}";', dts)
                self.assertIn(f"reg = <0x{address}>;", dts)

    def test_revision_discovery(self):
        choices = available_boards(str(ROOT))
        for revision in ("dvt", "dvt2"):
            with self.subTest(revision=revision):
                target = f"getafix@{revision}"
                self.assertIn(target, choices)
                board = parse_board(str(ROOT), target)
                self.assertEqual(board.revision, revision)
                self.assertEqual(board.normalized, f"getafix_{revision}")
                expected_runners = ["sftool", "probe-rs", "phone"]
                self.assertEqual(board.runners, expected_runners)


if __name__ == "__main__":
    unittest.main()
