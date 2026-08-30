# SPDX-License-Identifier: Apache-2.0

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_t5ai_image_map", ROOT / "tools/check_t5ai_image_map.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class TuyaImageMapTest(unittest.TestCase):
    def test_accepts_exact_slot_and_sections(self):
        text = """\
Memory Configuration
FLASH            0x02120000         0x001f0000         xr
.isr_vector     0x02120000      0x140
.text           0x02120140    0x10000
.kernel_data    0x28010000      0x100 load address 0x02130140
"""
        self.assertEqual(MODULE.validate_map(text), [])

    def test_rejects_broader_flash_region(self):
        text = """\
FLASH            0x02000000         0x00800000         xr
.text           0x02120000    0x10000
"""
        errors = MODULE.validate_map(text)
        self.assertTrue(any("FLASH region" in error for error in errors))

    def test_rejects_section_and_load_address_outside_slot(self):
        text = """\
FLASH            0x02120000         0x001f0000         xr
.text           0x02010000       0x20
.kernel_data    0x28010000       0x20 load address 0x02410000
"""
        errors = MODULE.validate_map(text)
        self.assertTrue(any("VMA" in error for error in errors))
        self.assertTrue(any("load address" in error for error in errors))

    def test_rejects_map_without_flash_outputs(self):
        text = "FLASH 0x02120000 0x001f0000 xr\n"
        errors = MODULE.validate_map(text)
        self.assertTrue(any("no output section" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
