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
        config = kconfiglib.Kconfig(str(ROOT / "pebble/Kconfig"), warn=False)
        paths = [f"boards/{board}/defconfig"]
        if revision != "_" and (ROOT / f"boards/{board}/defconfig.{revision}").exists():
            paths.append(f"boards/{board}/defconfig.{revision}")
        paths += ["pebble/prj.conf", "pebble/prj_normal.conf"]
        for index, path in enumerate(paths):
            config.load_config(str(ROOT / path), replace=index == 0)
        if power_savings is not None:
            config.syms["T5AI_POWER_SAVINGS"].set_value("y" if power_savings else "n")
        return config


class T5AIConfigTest(unittest.TestCase):
    def test_event_wait_defaults_on_with_explicit_fallback_in_both_power_profiles(self):
        for power_savings in (False, True):
            with self.subTest(power_savings=power_savings):
                config = load_config("unhygienix", power_savings=power_savings)
                self.assertEqual(config.syms["T5AI_HCI_EVENT_WAIT"].str_value, "y")
                self.assertEqual(config.syms["T5AI_HCI_PROFILE"].str_value, "n")
                config.syms["T5AI_HCI_EVENT_WAIT"].set_value("n")
                self.assertEqual(config.syms["T5AI_HCI_EVENT_WAIT"].str_value, "n")
                if power_savings:
                    self.assertEqual(
                        config.syms["T5AI_RETAINED_SLEEP_WAKE_ONLY"].str_value, "y"
                    )

    def test_graphics_bench_requires_all_paths_and_is_default_off(self):
        config = load_config("unhygienix")
        bench = config.syms["T5AI_GRAPHICS_BENCH"]
        self.assertEqual(bench.str_value, "n")
        bench.set_value("y")
        self.assertEqual(bench.str_value, "n")
        for name in (
            "T5AI_COMPOSITOR_FAST_PATH",
            "T5AI_PIXEL_FAST_PATH",
            "T5AI_TEXT_NODE_CACHE",
        ):
            config.syms[name].set_value("y")
        self.assertEqual(bench.str_value, "y")
        self.assertEqual(config.syms["T5AI_DRAW_PROFILE"].str_value, "n")
        config.syms["T5AI_TEXT_NODE_CACHE"].set_value("n")
        self.assertEqual(bench.str_value, "n")

    def test_motion_engine_defaults_on_without_diagnostics(self):
        config = load_config("unhygienix")
        for name in ("T5AI_QMI8658_ANY_MOTION", "T5AI_QMI8658_WOM"):
            self.assertEqual(config.syms[name].str_value, "y")
        for name in ("T5AI_QMI8658_PROFILE", "T5AI_I2C_PROFILE"):
            self.assertEqual(config.syms[name].str_value, "n")
        self.assertEqual(config.syms["DRIVER_IMU_LOG_LEVEL_DEBUG"].str_value, "n")
        config.syms["T5AI_QMI8658_ANY_MOTION"].set_value("n")
        self.assertEqual(config.syms["T5AI_QMI8658_ANY_MOTION"].str_value, "n")
        config.syms["T5AI_QMI8658_ANY_MOTION"].set_value("y")
        config.syms["T5AI_ACCEL_DISABLED"].set_value("y")
        self.assertEqual(config.syms["T5AI_QMI8658_ANY_MOTION"].str_value, "n")

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
            ("qemu_gabbro", "_", "SOC_QEMU_PEBBLE"),
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
                self.assertEqual(soc.str_value, "y")
                concrete = config.syms[
                    "SOC_QEMU_PEBBLE_CORTEX_M33"
                    if board == "qemu_gabbro"
                    else "SOC_SF32LB52J"
                ]
                self.assertIs(concrete.choice.selection, concrete)
                self.assertEqual(config.syms["SOC_T5AI"].str_value, "n")


if __name__ == "__main__":
    unittest.main()
