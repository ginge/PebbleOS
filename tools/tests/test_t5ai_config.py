# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]


def load_config(board, revision="_"):
    with patch.dict(
        os.environ,
        {"srctree": str(ROOT), "BOARD": board, "BOARD_REVISION": revision},
    ):
        config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn=False)
        paths = [f"boards/{board}/defconfig"]
        if revision != "_":
            paths.append(f"boards/{board}/defconfig.{revision}")
        paths += ["fw/prj.conf", "fw/prj_normal.conf"]
        for index, path in enumerate(paths):
            config.load_config(str(ROOT / path), replace=index == 0)
        return config


class T5AIConfigTest(unittest.TestCase):
    def test_header_cache_toggle_preserves_soc_and_transport(self):
        config = load_config("unhygienix")
        soc = config.syms["SOC_T5AI"]
        cache = config.syms["T5AI_PFS_HEADER_CACHE"]
        self.assertEqual(cache.str_value, "n")
        self.assertIsNone(cache.choice)
        for enabled in ("y", "n"):
            with self.subTest(cache=enabled):
                cache.set_value(enabled)
                self.assertEqual(cache.str_value, enabled)
                self.assertEqual(soc.str_value, "y")
                self.assertIs(soc.choice.selection, soc)
                self.assertEqual(config.syms["BT_HCI_T5AI"].str_value, "y")
                self.assertEqual(
                    config.syms["T5AI_CORE_CLOCK_HZ"].str_value, "120000000"
                )

    def test_t5ai_options_are_unavailable_on_stock_targets(self):
        for board, revision, soc_name in (
            ("qemu_gabbro", "_", "SOC_QEMU"),
            ("getafix", "dvt2", "SOC_SF32LB52"),
        ):
            with self.subTest(board=board):
                config = load_config(board, revision)
                soc = config.syms[soc_name]
                for name in ("T5AI_PFS_HEADER_CACHE", "T5AI_CP_HCI_VALIDATED"):
                    option = config.syms[name]
                    option.set_value("y")
                    self.assertEqual(option.str_value, "n")
                self.assertEqual(config.syms["T5AI_CORE_CLOCK_HZ"].str_value, "")
                self.assertIs(soc.choice.selection, soc)
                self.assertEqual(config.syms["SOC_T5AI"].str_value, "n")


if __name__ == "__main__":
    unittest.main()
