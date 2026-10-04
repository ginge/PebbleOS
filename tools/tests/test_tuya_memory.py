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

        compositor = (ROOT / "fw/services/compositor/compositor.c").read_text()
        self.assertIn('section(".t5ai_framebuffer")', compositor)

    def test_ap_only_nimble_pool_storage_uses_t5ai_psram(self):
        sections = (ROOT / "soc/t5ai/framebuffer.ld").read_text()
        self.assertIn("KEEP(*(.bss.os_msys_1_data))", sections)
        self.assertIn("KEEP(*(.bss.pool_acl_buf))", sections)
        self.assertIn("AP-only NimBLE pool backing arrays", sections)

    def test_framebuffer_psram_handoff_is_validated_before_use(self):
        compositor = (ROOT / "fw/services/compositor/compositor.c").read_text()
        validation = compositor.index("prv_validate_t5ai_framebuffer_ram()")
        initialization = compositor.index("framebuffer_init(&s_framebuffer")
        self.assertLess(validation, initialization)
        self.assertIn("PBL_ASSERTN(prv_validate_t5ai_framebuffer_ram())", compositor)
        self.assertIn("seed ^ (uint32_t)i", compositor)
        self.assertIn("~(seed ^ (uint32_t)i)", compositor)


if __name__ == "__main__":
    unittest.main()
