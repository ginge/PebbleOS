# SPDX-License-Identifier: Apache-2.0
"""Actual vendor/AP checksum units, without extracted functions or copied types."""

import os
import subprocess
import tempfile
import unittest

from tools.tests.t5ai_checksum_harness import build_checksums


@unittest.skipUnless(
    os.getenv("UNHYGIENIX_CRC_VENDOR_FLAGS"),
    "requires generated vendor CP config/include flags and 32-bit host toolchain",
)
class ChecksumTest(unittest.TestCase):
    def test_complete_vendor_and_ap_units_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = build_checksums(
                temporary, os.environ["UNHYGIENIX_CRC_VENDOR_FLAGS"]
            )
            result = subprocess.check_output([str(binary)], timeout=30, text=True)
            self.assertIn("vectors pass", result)
