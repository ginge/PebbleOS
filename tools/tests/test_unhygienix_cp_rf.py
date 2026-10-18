# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools import patch_unhygienix_cp_rf as rf
from tools.t5ai_image import encode_protected_blocks, verify_protected_blocks


class CPRFTest(unittest.TestCase):
    def setUp(self):
        image = bytearray(rf.RESET_OFFSET + len(rf.STOCK_RESET))
        image[rf.RESET_OFFSET :] = rf.STOCK_RESET
        self.image = bytes(image)

    def test_original_bl_encoding_matches_matching_elf(self):
        self.assertEqual(
            rf.thumb_bl(rf.CP_XIP + rf.INIT_CALL_OFFSET, rf.INIT_XIP),
            bytes.fromhex("05f046fa"),
        )
        for destination in [1, rf.CP_XIP + (1 << 25)]:
            with self.assertRaises(ValueError):
                rf.thumb_bl(rf.CP_XIP + rf.INIT_CALL_OFFSET, destination)

    def test_only_init_call_changes_existing_bytes_and_crc_roundtrips(self):
        hook = bytes.fromhex("00207047")
        with patch.object(rf, "STOCK_SHA256", hashlib.sha256(self.image).hexdigest()):
            result = rf.install_hook(self.image, hook)
        self.assertEqual(
            result[: rf.INIT_CALL_OFFSET], self.image[: rf.INIT_CALL_OFFSET]
        )
        self.assertEqual(
            result[rf.INIT_CALL_OFFSET + 4 : len(self.image)],
            self.image[rf.INIT_CALL_OFFSET + 4 :],
        )
        self.assertEqual(result[-len(hook) :], hook)
        self.assertEqual(
            verify_protected_blocks(encode_protected_blocks(result))[: len(result)],
            result,
        )

    def test_unknown_vendor_image_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA256"):
            rf.install_hook(self.image, b"hook")

    def test_wrong_reset_rejected_even_with_matching_digest(self):
        image = bytearray(self.image)
        image[rf.RESET_OFFSET] = 3
        with patch.object(rf, "STOCK_SHA256", hashlib.sha256(image).hexdigest()):
            with self.assertRaisesRegex(ValueError, "reset function"):
                rf.install_hook(bytes(image), b"hook")

    def test_real_source_applies_configuration_after_successful_init_only(self):
        gcc = shutil.which("gcc")
        if gcc is None:
            self.skipTest("host C compiler unavailable")
        harness = r"""
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
uint16_t rwnx_rfconfig = 0x101;
static unsigned step;
static int status;
int bk_bluetooth_init(void) { assert(step++ == 0); return status; }
unsigned rwnx_cal_set_rfconfig(unsigned value) { assert(step++ == 1); assert(value == 0x202); rwnx_rfconfig = value; return value; }
void bk_printf(const char *fmt, ...) { assert(step++ == 2); assert(fmt != 0); va_list ap; va_start(ap,fmt); assert(va_arg(ap,unsigned) == 0x202); va_end(ap); }
extern int pebble_ble_init_bt_pll(void);
int main(void) {
  assert(pebble_ble_init_bt_pll() == 0); assert(step == 3); assert(rwnx_rfconfig == 0x202);
  step = 0; status = -7; rwnx_rfconfig = 0x101;
  assert(pebble_ble_init_bt_pll() == -7); assert(step == 1); assert(rwnx_rfconfig == 0x101);
}
"""
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            test = folder / "test.c"
            test.write_text(harness)
            exe = folder / "test"
            subprocess.run(
                [
                    gcc,
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(rf.SOURCE),
                    str(test),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run([str(exe)], check=True)
