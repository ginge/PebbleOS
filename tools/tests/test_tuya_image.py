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

from tools.t5ai_image import (
    PEBBLE_FILESYSTEM_LOGICAL_OFFSET,
    PEBBLE_FILESYSTEM_SIZE,
    PEBBLE_RESOURCES_LOGICAL_OFFSET,
    build_pebble_flash_image,
    verify_protected_blocks,
)


class TuyaImageMapTest(unittest.TestCase):
    LAYOUT = """\
KERNEL_RAM       0x28010000         0x0001a800         xrw
WORKER_RAM       0x2802a800         0x00003000         xrw
APP_RAM          0x2802d800         0x00038800         xrw
T5AI_DISPLAY_RAM 0x60000000         0x00020000         rw
                0x28011000 __isr_stack_start__ = .
                0x28012000 __kernel_main_stack_start__ = .
                0x28013000 __kernel_bg_stack_start__ = .
                0x28014000 _heap_start = .
                0x2802a800 _heap_end = .
                0x60000000 __t5ai_framebuffer_start__ = .
                0x6001081e __t5ai_framebuffer_end__ = .
"""

    def test_accepts_exact_slot_and_sections(self):
        text = """\
Memory Configuration
FLASH            0x02120000         0x000f0000         xr
.isr_vector     0x02120000      0x140
.text           0x02120140    0x10000
.kernel_data    0x28010000      0x100 load address 0x02130140
""" + self.LAYOUT
        self.assertEqual(MODULE.validate_map(text), [])

    def test_rejects_broader_flash_region(self):
        text = """\
FLASH            0x02000000         0x00800000         xr
.text           0x02120000    0x10000
""" + self.LAYOUT
        errors = MODULE.validate_map(text)
        self.assertTrue(any("FLASH region" in error for error in errors))

    def test_rejects_section_and_load_address_outside_slot(self):
        text = """\
FLASH            0x02120000         0x000f0000         xr
.isr_vector     0x02120000      0x140
.text           0x02010000       0x20
.kernel_data    0x28010000       0x20 load address 0x02410000
""" + self.LAYOUT
        errors = MODULE.validate_map(text)
        self.assertTrue(any("VMA" in error for error in errors))
        self.assertTrue(any("load address" in error for error in errors))

    def test_rejects_map_without_flash_outputs(self):
        text = "FLASH 0x02120000 0x000f0000 xr\n"
        errors = MODULE.validate_map(text)
        self.assertTrue(any("no output section" in error for error in errors))

    def test_rejects_bad_runtime_layout_and_vendor_objects(self):
        text = """\
FLASH            0x02120000         0x000f0000         xr
.isr_vector     0x02120004      0x13c
.text           0x02120140    0x10000 boards/getafix/tuya/src/main.c.1.o
""" + self.LAYOUT.replace("0x28014000 _heap_start", "0x2802b000 _heap_start")
        errors = MODULE.validate_map(text)
        self.assertTrue(any("isr_vector" in error for error in errors))
        self.assertTrue(any("stack/heap" in error for error in errors))
        self.assertTrue(any("vendor/reference" in error for error in errors))

    def test_flash_image_contains_firmware_resources_and_clean_pfs(self):
        firmware = b"firmware"
        resources = b"resources"
        protected = build_pebble_flash_image(firmware, resources)
        logical = verify_protected_blocks(protected)

        self.assertEqual(logical[: len(firmware)], firmware)
        resource_start = PEBBLE_RESOURCES_LOGICAL_OFFSET
        self.assertEqual(
            logical[resource_start : resource_start + len(resources)], resources
        )
        filesystem_start = PEBBLE_FILESYSTEM_LOGICAL_OFFSET
        self.assertEqual(
            logical[filesystem_start : filesystem_start + PEBBLE_FILESYSTEM_SIZE],
            b"\xff" * PEBBLE_FILESYSTEM_SIZE,
        )

    def test_flash_image_rejects_region_overflow(self):
        with self.assertRaises(ValueError):
            build_pebble_flash_image(
                b"x" * (PEBBLE_RESOURCES_LOGICAL_OFFSET + 1), b""
            )
        with self.assertRaises(ValueError):
            build_pebble_flash_image(b"", b"x" * (0x100000 + 1))


if __name__ == "__main__":
    unittest.main()
