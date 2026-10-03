# SPDX-License-Identifier: Apache-2.0
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import patch_t5ai_cp_rf as rf
from tools.t5ai_image import encode_protected_blocks, verify_protected_blocks


class CPRFTest(unittest.TestCase):
    def setUp(self):
        self.image = bytearray(rf.RF_MODE_OFFSET + len(rf.STOCK_RF_MODE) + 4)
        self.image[rf.RF_MODE_OFFSET:rf.RF_MODE_OFFSET + len(rf.STOCK_RF_MODE)] = rf.STOCK_RF_MODE
        self.image[-4:] = b"keep"
        self.image = bytes(self.image)

    def test_only_callback_entry_changes_and_crc_roundtrips(self):
        with patch.object(rf, "STOCK_SHA256", hashlib.sha256(self.image).hexdigest()):
            result = rf.select_ble_pll(self.image)
        self.assertEqual(result[:rf.RF_MODE_OFFSET], self.image[:rf.RF_MODE_OFFSET])
        self.assertEqual(result[rf.RF_MODE_OFFSET:rf.RF_MODE_OFFSET + len(rf.BLE_RF_MODE)], rf.BLE_RF_MODE)
        self.assertEqual(result[rf.RF_MODE_OFFSET + len(rf.BLE_RF_MODE):], self.image[rf.RF_MODE_OFFSET + len(rf.BLE_RF_MODE):])
        self.assertEqual(len(result), len(self.image))
        self.assertEqual(verify_protected_blocks(encode_protected_blocks(result))[:len(result)], result)

    def test_unknown_vendor_image_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA256"):
            rf.select_ble_pll(self.image)

    def test_wrong_callback_rejected_even_with_matching_digest(self):
        image = bytearray(self.image)
        image[rf.RF_MODE_OFFSET] = 3
        with patch.object(rf, "STOCK_SHA256", hashlib.sha256(image).hexdigest()):
            with self.assertRaisesRegex(ValueError, "callback"):
                rf.select_ble_pll(bytes(image))

    def test_source_compiles_to_installed_leaf_function(self):
        gcc = shutil.which("arm-none-eabi-gcc")
        if gcc is None:
            self.skipTest("ARM compiler unavailable")
        objcopy = str(Path(gcc).with_name("arm-none-eabi-objcopy"))
        source = Path(__file__).resolve().parents[2] / "boards/getafix/tuya/cp/ble_rf_mode.c"
        with tempfile.TemporaryDirectory() as folder:
            obj = Path(folder) / "rf.o"
            raw = Path(folder) / "rf.bin"
            subprocess.run([gcc, "-Os", "-mthumb", "-mcpu=cortex-m33", "-c", str(source), "-o", str(obj)], check=True)
            subprocess.run([objcopy, "-O", "binary", "-j", ".text", str(obj), str(raw)], check=True)
            self.assertEqual(raw.read_bytes(), rf.BLE_RF_MODE)
