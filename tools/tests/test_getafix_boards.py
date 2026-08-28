# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.waf.boards import available_boards, parse_board


def load_config(revision, variant):
    with patch.dict(os.environ, {
        'srctree': str(ROOT),
        'BOARD': 'getafix',
        'BOARD_REVISION': revision,
    }):
        config = kconfiglib.Kconfig(str(ROOT / 'Kconfig'), warn=False)
        for index, path in enumerate([
            'boards/getafix/defconfig',
            f'boards/getafix/defconfig.{revision}',
            'src/fw/prj.conf',
            f'src/fw/prj_{variant}.conf',
        ]):
            config.load_config(str(ROOT / path), replace=index == 0)
        return {
            name: symbol.str_value
            for name, symbol in config.syms.items() if symbol.nodes
        }


class GetafixBoardsTest(unittest.TestCase):
    def test_revision_discovery(self):
        choices = available_boards(str(ROOT))
        for revision in ('dvt', 'dvt2', 'dvt3'):
            with self.subTest(revision=revision):
                target = f'getafix@{revision}'
                self.assertIn(target, choices)
                board = parse_board(str(ROOT), target)
                self.assertEqual(board.revision, revision)
                self.assertEqual(board.normalized, f'getafix_{revision}')

    def test_dvt3_inherits_dvt2_without_aliasing_identity(self):
        for variant in ('normal', 'prf'):
            with self.subTest(variant=variant):
                dvt2 = load_config('dvt2', variant)
                dvt3 = load_config('dvt3', variant)
                self.assertEqual(dvt3.pop('BOARD_GETAFIX_DVT3'), 'y')
                self.assertNotIn('BOARD_GETAFIX_DVT2', dvt3)
                self.assertEqual(dvt2.pop('BOARD_GETAFIX_DVT2'), 'y')
                self.assertEqual(dvt2, dvt3)
                self.assertEqual(dvt3['VIBE_AW86225'], 'y')
                self.assertEqual(dvt3['VIBE_AW8623X'], 'n')

    def test_original_dvt_keeps_its_haptics(self):
        config = load_config('dvt', 'normal')
        self.assertEqual(config['VIBE_AW8623X'], 'y')
        self.assertEqual(config['VIBE_AW86225'], 'n')


if __name__ == '__main__':
    unittest.main()
