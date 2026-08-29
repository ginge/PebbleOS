# SPDX-License-Identifier: Apache-2.0
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaMemoryLayoutTest(unittest.TestCase):
    def test_internal_ap_window_matches_pinned_bsp(self):
        linker = (ROOT / "soc/t5ai/linker.ld").read_text()
        self.assertIn("0x28010000", linker)
        self.assertIn("0x28066000", linker)

    def test_framebuffer_has_bounded_psram_region(self):
        memory = (ROOT / "soc/t5ai/psram-memory.ld").read_text()
        match = re.search(
            r"T5AI_DISPLAY_RAM\s+\(rw\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-fA-F]+),"
            r"\s*LENGTH\s*=\s*(\d+)K",
            memory,
        )
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1), 16), 0x60000000)
        self.assertLessEqual(int(match.group(2)), 128)

        compositor = (ROOT / "src/fw/services/compositor/compositor.c").read_text()
        self.assertIn('section(".t5ai_framebuffer")', compositor)


if __name__ == "__main__":
    unittest.main()
