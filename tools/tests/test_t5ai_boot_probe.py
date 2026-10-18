# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.unhygienix_image import PRIMARY_AP_APP_XIP_BASE, verify_protected_blocks


ROOT = Path(__file__).resolve().parents[2]


class T5AIBootProbeTest(unittest.TestCase):
    def test_probe_builds_with_valid_vectors_and_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run(
                [
                    str(ROOT / ".venv" / "bin" / "python"),
                    str(ROOT / "tools" / "build_t5ai_boot_probe.py"),
                    "--output-dir",
                    directory,
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
            )
            protected = Path(directory, "t5ai_boot_probe.t5ai-crc.bin").read_bytes()
            raw = verify_protected_blocks(protected)
            self.assertEqual(int.from_bytes(raw[0:4], "little"), 0x28066000)
            reset = int.from_bytes(raw[4:8], "little")
            self.assertTrue(reset & 1)
            self.assertGreaterEqual(reset & ~1, PRIMARY_AP_APP_XIP_BASE)
            self.assertLess(reset & ~1, PRIMARY_AP_APP_XIP_BASE + 4096)
            self.assertEqual(len(raw[:0x100]), 0x100)
            self.assertIn(b"PBL-CPU1-RESET", raw)
            self.assertIn((0x10010001).to_bytes(4, "little"), raw)
            self.assertIn((0x10020002).to_bytes(4, "little"), raw)
            self.assertIn((0x28010000).to_bytes(4, "little"), raw)


if __name__ == "__main__":
    unittest.main()
