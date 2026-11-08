# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib

ROOT = Path(__file__).resolve().parents[2]


def load_config(board, revision="_", power_savings=None):
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
        if power_savings is not None:
            config.syms["T5AI_POWER_SAVINGS"].set_value("y" if power_savings else "n")
        return config


class T5AIConfigTest(unittest.TestCase):
    def test_sd_pfs_is_board_default_with_explicit_nor_override(self):
        config = load_config("unhygienix", power_savings=False)
        self.assertEqual(config.syms["T5AI_SD_PFS"].str_value, "y")
        self.assertEqual(config.syms["T5AI_SD_4BIT"].str_value, "y")
        self.assertEqual(config.syms["T5AI_SD_PFS_REQUIRE_EXISTING"].str_value, "y")
        config.syms["T5AI_SD_PFS"].set_value("n")
        self.assertEqual(config.syms["T5AI_SD_PFS"].str_value, "n")
        self.assertEqual(config.syms["T5AI_SD_4BIT"].str_value, "n")
        self.assertEqual(config.syms["FLASH_T5AI"].str_value, "y")

    def test_sd_idle_gate_is_opt_in_and_requires_sd_pfs(self):
        config = load_config("unhygienix", power_savings=False)
        gate = config.syms["T5AI_SD_IDLE_GATING"]
        self.assertEqual(gate.str_value, "n")
        gate.set_value("y")
        self.assertEqual(gate.str_value, "y")
        config.syms["T5AI_SD_PFS"].set_value("n")
        self.assertEqual(gate.str_value, "n")

    def test_power_qualification_options_are_opt_in(self):
        config = load_config("unhygienix", power_savings=False)
        for name in [
            "T5AI_PM_WAKE_EVENTS",
            "T5AI_POWER_PROFILE",
            "T5AI_UART_IDLE_GATING",
            "T5AI_BATTERY_QUIET_SUCCESS",
            "T5AI_AMOLED_ZERO_BRIGHTNESS",
            "T5AI_AMOLED_SLEEP_ON_DISABLE",
        ]:
            self.assertEqual(config.syms[name].str_value, "n")
        self.assertEqual(config.syms["T5AI_UART_IDLE_GATING"].str_value, "n")
        config.syms["T5AI_POWER_PROFILE"].set_value("y")
        self.assertEqual(config.syms["T5AI_POWER_PROFILE"].str_value, "n")
        config.syms["T5AI_TICK_PROFILE"].set_value("y")
        self.assertEqual(config.syms["T5AI_POWER_PROFILE"].str_value, "y")

    def test_retained_sleep_requires_native_contract_and_excludes_tick_extension(self):
        config = load_config("unhygienix", power_savings=False)
        for name in [
            "T5AI_RETAINED_SLEEP",
            "T5AI_RETAINED_SLEEP_TEST",
            "T5AI_NATIVE_ALARM",
            "T5AI_NATIVE_ALARM_TEST",
        ]:
            self.assertEqual(config.syms[name].str_value, "n")
        config.syms["T5AI_RETAINED_SLEEP"].set_value("y")
        self.assertEqual(config.syms["T5AI_NATIVE_ALARM"].str_value, "y")
        self.assertEqual(config.syms["RAMFUNC"].str_value, "y")
        config.syms["T5AI_RETAINED_SLEEP_TEST"].set_value("y")
        self.assertEqual(config.syms["T5AI_RETAINED_SLEEP_TEST_AT_UTC"].str_value, "0")
        config.syms["T5AI_NATIVE_ALARM_TEST"].set_value("y")
        self.assertEqual(config.syms["T5AI_NATIVE_ALARM_TEST"].str_value, "n")
        config.syms["T5AI_TICKLESS_WFI"].set_value("y")
        self.assertEqual(config.syms["T5AI_RETAINED_SLEEP"].str_value, "n")

    def test_ap_only_policy_is_available_without_bounded_test(self):
        config = load_config("unhygienix", power_savings=False)
        config.syms["T5AI_RETAINED_SLEEP"].set_value("y")
        config.syms["T5AI_RETAINED_SLEEP_WAKE_ONLY"].set_value("n")
        self.assertEqual(config.syms["T5AI_RETAINED_SLEEP_TEST"].str_value, "n")
        self.assertEqual(config.syms["T5AI_RETAINED_SLEEP_WAKE_ONLY"].str_value, "y")
        self.assertEqual(config.syms["T5AI_NATIVE_ALARM"].str_value, "y")

    def test_board_defaults_keep_cp_awake_and_preserve_dim_display(self):
        config = load_config("unhygienix")
        self.assertEqual(config.syms["T5AI_POWER_SAVINGS"].str_value, "y")
        for name in (
            "T5AI_RETAINED_SLEEP",
            "T5AI_RETAINED_SLEEP_WAKE_ONLY",
            "T5AI_SD_IDLE_GATING",
            "T5AI_I2C_IDLE_GATING",
            "T5AI_UART_IDLE_GATING",
            "T5AI_BATTERY_QUIET_SUCCESS",
        ):
            self.assertEqual(config.syms[name].str_value, "y")
        for name in (
            "T5AI_RETAINED_SLEEP_TEST",
            "T5AI_TICKLESS_WFI",
            "T5AI_AMOLED_ZERO_BRIGHTNESS",
            "T5AI_AMOLED_SLEEP_ON_DISABLE",
        ):
            self.assertEqual(config.syms[name].str_value, "n")
        # A production assignment cannot enable the CP-sleep fallback.
        config.syms["T5AI_RETAINED_SLEEP_WAKE_ONLY"].set_value("n")
        self.assertEqual(config.syms["T5AI_RETAINED_SLEEP_WAKE_ONLY"].str_value, "y")

    def test_header_cache_toggle_preserves_soc_and_transport(self):
        config = load_config("unhygienix", power_savings=False)
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
                for name in [
                    "T5AI_SD_IDLE_GATING",
                    "T5AI_PM_WAKE_EVENTS",
                    "T5AI_POWER_PROFILE",
                ]:
                    self.assertNotIn(name, config.syms)
                for name in ("T5AI_PFS_HEADER_CACHE", "T5AI_CP_HCI_VALIDATED"):
                    option = config.syms[name]
                    option.set_value("y")
                    self.assertEqual(option.str_value, "n")
                self.assertEqual(config.syms["T5AI_CORE_CLOCK_HZ"].str_value, "")
                self.assertIs(soc.choice.selection, soc)
                self.assertEqual(config.syms["SOC_T5AI"].str_value, "n")


if __name__ == "__main__":
    unittest.main()
