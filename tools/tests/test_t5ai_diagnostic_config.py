# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import itertools
import os
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]


def load_config():
    with patch.dict(os.environ, {"srctree": str(ROOT), "BOARD": "unhygienix",
                                "BOARD_REVISION": "_"}):
        config = kconfiglib.Kconfig(str(ROOT / "Kconfig"), warn=False)
        for index, path in enumerate(("boards/unhygienix/defconfig", "fw/prj.conf",
                                      "fw/prj_normal.conf")):
            config.load_config(str(ROOT / path), replace=index == 0)
        return config


class DiagnosticConfigTest(unittest.TestCase):
    def test_diagnostics_remain_individually_available_and_default_off(self):
        names = ("T5AI_BG_TRACE", "T5AI_APP_TRACE", "T5AI_REMINDER_PROFILE",
                 "T5AI_WDT_RECOVERY_TRACE", "T5AI_TASK_CPU_METRICS", "T5AI_BG_PICK_TRACE")
        for name in names:
            with self.subTest(name=name):
                config = load_config()
                self.assertEqual(config.syms[name].str_value, "n")
                config.syms[name].set_value("y")
                self.assertEqual(config.syms[name].str_value, "y")

    def test_conflicting_wrappers_cannot_be_selected(self):
        pairs = (("T5AI_BG_TRACE", "T5AI_REMINDER_PROFILE"),
                 ("T5AI_BG_TRACE", "T5AI_WDT_RECOVERY_TRACE"),
                 ("T5AI_REMINDER_PROFILE", "T5AI_WDT_RECOVERY_TRACE"),
                 ("T5AI_TASK_CPU_METRICS", "T5AI_BG_PICK_TRACE"),
                 ("T5AI_APP_TRACE", "T5AI_REMINDER_PROFILE"),
                 ("T5AI_APP_TRACE", "T5AI_WDT_RECOVERY_TRACE"))
        for pair in pairs:
            for order in itertools.permutations(pair):
                with self.subTest(order=order):
                    config = load_config()
                    for name in order:
                        config.syms[name].set_value("y")
                    self.assertFalse(all(config.syms[name].str_value == "y" for name in pair))
                    self.assertTrue(any(config.syms[name].str_value == "y" for name in pair))
                    self.assertFalse(config.warnings)

    def test_compatible_profiles_remain_composable(self):
        for names in (("T5AI_TASK_CPU_METRICS", "T5AI_REMINDER_PROFILE"),
                      ("T5AI_BG_PICK_TRACE", "T5AI_WDT_RECOVERY_TRACE")):
            with self.subTest(names=names):
                config = load_config()
                for name in names:
                    config.syms[name].set_value("y")
                self.assertTrue(all(config.syms[name].str_value == "y" for name in names))


if __name__ == "__main__":
    unittest.main()
