# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import kconfiglib
import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.boards import available_boards, parse_board


def load_config(board, variant):
    with patch.dict(
        os.environ,
        {"srctree": str(ROOT), "BOARD": board, "BOARD_REVISION": "_"},
    ):
        config = kconfiglib.Kconfig(str(ROOT / "pebble/Kconfig"), warn=False)
        paths = (
            f"boards/{board}/defconfig",
            "pebble/prj.conf",
            f"pebble/prj_{variant}.conf",
        )
        for index, path in enumerate(paths):
            config.load_config(str(ROOT / path), replace=index == 0)
        return {
            name: symbol.str_value
            for name, symbol in config.syms.items()
            if symbol.nodes
        }


class UnhygienixBoardsTest(unittest.TestCase):
    def test_unhygienix_only_exposes_exact_range_flash_runner(self):
        unhygienix = parse_board(str(ROOT), "unhygienix")
        self.assertEqual(unhygienix.runners, ["tyutool"])

        runner = (ROOT / "tools/libs/pbl-cli/pbl/runners/tyutool.py").read_text()
        self.assertIn('commands={"flash"}', runner)
        self.assertIn("end = start + len(image)", runner)
        self.assertNotIn('"erase"', runner)

    def test_unhygienix_internal_update_paths_are_fail_closed(self):
        build_script = (ROOT / "soc/t5ai/CMakeLists.txt").read_text() + (
            ROOT / "tools/cmake/firmware.py"
        ).read_text()
        self.assertIn("T5AI recovery firmware is disabled", build_script)
        self.assertIn("Unhygienix has no supported PBZ update flow", build_script)

        firmware_update = (
            ROOT / "pebble/services/firmware_update/service.c"
        ).read_text()
        self.assertIn("#ifndef CONFIG_FIRMWARE_UPDATE_SUPPORTED", firmware_update)
        self.assertIn("return FirmwareUpdateCancelled;", firmware_update)

        put_bytes = (ROOT / "pebble/services/put_bytes/put_bytes.c").read_text()
        t5ai_gate = put_bytes.index("Update object type %u is unavailable")
        for object_name in ("ObjectFirmware", "ObjectRecovery", "ObjectSysResources"):
            self.assertIn(object_name, put_bytes[t5ai_gate - 400 : t5ai_gate])

        prf_update = (ROOT / "pebble/services/prf_update/service.c").read_text()
        self.assertIn("Ignoring unsupported recovery firmware update", prf_update)

    def test_unhygienix_reports_round_2_identity_without_enabling_updates(self):
        metadata = (ROOT / "include/pebbleos/firmware_metadata.h").read_text()
        unhygienix_identity = metadata[
            metadata.index("#elif defined(CONFIG_BOARD_UNHYGIENIX)") : metadata.index(
                "#elif defined(CONFIG_BOARD_QEMU_EMERY)"
            )
        ]
        self.assertIn("FirmwareMetadataPlatformPebbleGetafixDVT2", unhygienix_identity)
        self.assertNotIn("FirmwareMetadataPlatformUnknown", unhygienix_identity)

        versions = (ROOT / "pebble/kernel/system_versions.c").read_text()
        compatibility_start = versions.index("#if defined(CONFIG_BOARD_UNHYGIENIX)")
        compatibility = versions[
            compatibility_start : versions.index("#else", compatibility_start)
        ]
        self.assertIn("if (!has_recovery_fw)", compatibility)
        self.assertIn(
            "version_copy_running_fw_metadata(&versions_msg.recovery_fw_metadata)",
            compatibility,
        )
        self.assertIn(
            "versions_msg.recovery_fw_metadata.is_recovery_firmware = true",
            compatibility,
        )

        # The compatibility record is phone-facing only. Real PRF detection and
        # the fail-closed firmware transfer gates must remain in place.
        version = (ROOT / "pebble/system/version.c").read_text()
        self.assertIn("bool version_is_prf_installed(void)", version)
        firmware_update = (
            ROOT / "pebble/services/firmware_update/service.c"
        ).read_text()
        self.assertIn("Firmware update is unavailable", firmware_update)
        put_bytes = (ROOT / "pebble/services/put_bytes/put_bytes.c").read_text()
        rejection = put_bytes.index("Update object type %u is unavailable")
        for object_name in ("ObjectFirmware", "ObjectRecovery", "ObjectSysResources"):
            self.assertIn(object_name, put_bytes[rejection - 400 : rejection])

    def test_unhygienix_shared_prf_storage_uses_reserved_writable_data_partition(self):
        shared_prf = (
            ROOT / "pebble/services/shared_prf_storage/shared_prf_storage.c"
        ).read_text()
        regions = (ROOT / "soc/t5ai/compat/flash_region/flash_region.h").read_text()
        flash = (ROOT / "drivers/flash/t5ai.c").read_text()
        factory_reset = (ROOT / "pebble/kernel/util/factory_reset.c").read_text()

        self.assertNotIn("CONFIG_SOC_T5AI", shared_prf)
        self.assertIn("pbl_flash_partition_erase(PART_SHARED_PRF_STORAGE,", shared_prf)
        self.assertIn("FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x024ea000", regions)
        self.assertRegex(regions, r"FLASH_REGION_SHARED_PRF_STORAGE_END\s+0x024fa000")
        self.assertIn("in_shared_prf", flash)
        self.assertIn("shared_prf_storage_wipe_all();", factory_reset)

    def test_unhygienix_has_native_t5ai_identity(self):
        unhygienix = load_config("unhygienix", "normal")
        self.assertEqual(unhygienix["BOARD_UNHYGIENIX"], "y")
        self.assertNotIn("BOARD_GETAFIX", unhygienix)
        self.assertEqual(unhygienix["SOC_T5AI"], "y")
        self.assertEqual(unhygienix["CPU_CORTEX_M33"], "y")
        self.assertEqual(unhygienix["PLATFORM_GABBRO"], "y")
        self.assertEqual(unhygienix["MODDABLE_XS"], "y")
        self.assertEqual(unhygienix["VIBE"], "y")
        self.assertEqual(unhygienix["PMIC"], "n")
        self.assertEqual(unhygienix["MAG"], "n")
        self.assertEqual(unhygienix["ACCEL_QMI8658_T5AI"], "y")
        self.assertEqual(unhygienix["ACCEL_LIS2DW12"], "n")
        self.assertEqual(unhygienix["BATTERY_T5AI"], "y")
        self.assertNotIn("BATTERY_PMIC", unhygienix)
        self.assertEqual(unhygienix["BT_HCI_T5AI"], "y")
        self.assertEqual(unhygienix["T5AI_CP_HCI_VALIDATED"], "y")
        self.assertEqual(unhygienix["SHELL"], "n")
        self.assertEqual(unhygienix["SPEAKER"], "n")
        self.assertEqual(unhygienix["MIC"], "y")
        self.assertNotIn("MIC_T5AI", unhygienix)
        self.assertEqual(unhygienix["SERVICE_VOICE"], "y")
        self.assertEqual(unhygienix["VIBE"], "y")
        self.assertEqual(unhygienix["ALS"], "y")
        self.assertNotIn("PRESSURE", unhygienix)
        self.assertNotIn("TEMPERATURE", unhygienix)

    def test_unavailable_hardware_roles_have_no_device(self):
        roles = yaml.safe_load((ROOT / "boards/unhygienix/roles.yml").read_text())
        for name in ("vibe", "als", "die_temp"):
            with self.subTest(role=name):
                self.assertEqual(roles[name]["symbol"], "nullptr")

    def test_independent_board_discovery(self):
        choices = available_boards(str(ROOT))
        self.assertIn("unhygienix", choices)
        board = parse_board(str(ROOT), "unhygienix")
        self.assertEqual(board.normalized, "unhygienix")
        self.assertIsNone(board.revision)
        self.assertEqual(board.runners, ["tyutool"])
        with self.assertRaisesRegex(ValueError, "Unknown board"):
            parse_board(str(ROOT), "unknown-board")


if __name__ == "__main__":
    unittest.main()
