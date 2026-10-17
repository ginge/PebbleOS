# SPDX-License-Identifier: Apache-2.0

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.boards import BoardSpec, available_boards, parse_board


class BoardMetadataTest(unittest.TestCase):
    def test_qemu_decorations_reach_cli_board_spec(self):
        board = parse_board(str(ROOT), "qemu_gabbro")
        self.assertEqual(board.qemu, {"decorations": ["pr2-bk20", "pr2-gd14"]})
        self.assertEqual(board.runners, [])

    def test_every_discovered_board_exposes_qemu_metadata(self):
        for target in available_boards(str(ROOT)):
            with self.subTest(target=target):
                self.assertIsInstance(parse_board(str(ROOT), target).qemu, dict)

    def test_empty_qemu_metadata_is_independent_per_board(self):
        board = BoardSpec("first", "first")
        board.qemu["decorations"] = ["test"]
        self.assertEqual(BoardSpec("second", "second").qemu, {})
        self.assertEqual(parse_board(str(ROOT), "unhygienix").qemu, {"decorations": []})
        self.assertEqual(parse_board(str(ROOT), "unhygienix").runners, ["tyutool"])

    def test_invalid_qemu_metadata_is_rejected(self):
        for metadata in ("qemu: []", "qemu: {decorations: invalid}",
                         "qemu: {decorations: [one, one]}"):
            with self.subTest(metadata=metadata), tempfile.TemporaryDirectory() as directory:
                board_dir = Path(directory) / "boards/example"
                board_dir.mkdir(parents=True)
                (board_dir / "example.yml").write_text(metadata)
                with self.assertRaises((TypeError, ValueError)):
                    parse_board(directory, "example")


if __name__ == "__main__":
    unittest.main()
