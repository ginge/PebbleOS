# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_unhygienix_image_map", ROOT / "tools/check_unhygienix_image_map.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

from tools.unhygienix_image import (
    PEBBLE_AP_PHYSICAL_OFFSET,
    PEBBLE_FILESYSTEM_PHYSICAL_OFFSET,
    PEBBLE_FILESYSTEM_SIZE,
    PEBBLE_AP_SLOT_SIZE,
    PEBBLE_RESOURCES_LOGICAL_OFFSET,
    PEBBLE_RESOURCES_SIZE,
    PFS_ERASED_PAGE_FLAGS,
    PFS_INITIAL_ERASE_COUNT,
    PFS_PAGE_SIZE,
    PFS_VERSION,
    build_empty_pfs,
    build_pebble_flash_image,
    verify_protected_blocks,
)


class UnhygienixImageMapTest(unittest.TestCase):
    LAYOUT = """\
RAM              0x28010000         0x0001f000         xrw
                0x2802f000 __WORKER_RAM__ = .
                0x00003000 __WORKER_RAM_size__ = .
                0x28032000 __APP_RAM__ = .
                0x00034000 __APP_RAM_size__ = .
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
        text = (
            """\
Memory Configuration
FLASH            0x02120000         0x00190000         xr
.isr_vector     0x02120000      0x140
.text           0x02120140    0x10000
.kernel_data    0x28010000      0x100 load address 0x02130140
"""
            + self.LAYOUT
        )
        self.assertEqual(MODULE.validate_map(text), [])

    def test_sd_pfs_flags_have_a_separate_bounded_ap_region(self):
        base = ("FLASH 0x02120000 0x00190000 xr\n"
                ".isr_vector 0x02120000 0x140\n" + self.LAYOUT)
        flags = ("T5AI_PFS_RAM 0x60020000 0x2000 rw\n"
                 ".t5ai_pfs_flags 0x60020000 0x2000\n")
        self.assertEqual(MODULE.validate_map(base + flags), [])
        wrapped = flags.replace(".t5ai_pfs_flags 0x", ".t5ai_pfs_flags\n                0x")
        self.assertEqual(MODULE.validate_map(base + wrapped), [])
        for bad in (flags.replace("0x60020000", "0x60700000"),
                    flags.replace("0x2000 rw", "0x4000 rw"),
                    flags.replace(".t5ai_pfs_flags 0x60020000 0x2000",
                                  ".t5ai_pfs_flags 0x60020000 0x2004"),
                    flags.splitlines()[1] + "\n", flags.splitlines()[0] + "\n"):
            with self.subTest(layout=bad):
                self.assertTrue(any("SD PFS" in e for e in MODULE.validate_map(base + bad)))

    def test_rejects_broader_flash_region(self):
        text = (
            """\
FLASH            0x02000000         0x00800000         xr
.text           0x02120000    0x10000
"""
            + self.LAYOUT
        )
        errors = MODULE.validate_map(text)
        self.assertTrue(any("FLASH region" in error for error in errors))

    def test_rejects_section_and_load_address_outside_slot(self):
        text = (
            """\
FLASH            0x02120000         0x00190000         xr
.isr_vector     0x02120000      0x140
.text           0x02010000       0x20
.kernel_data    0x28010000       0x20 load address 0x02410000
"""
            + self.LAYOUT
        )
        errors = MODULE.validate_map(text)
        self.assertTrue(any("VMA" in error for error in errors))
        self.assertTrue(any("load address" in error for error in errors))

    def test_rejects_map_without_flash_outputs(self):
        text = "FLASH 0x02120000 0x00190000 xr\n"
        errors = MODULE.validate_map(text)
        self.assertTrue(any("no output section" in error for error in errors))

    def test_rejects_bad_runtime_layout_and_vendor_objects(self):
        text = """\
FLASH            0x02120000         0x00190000         xr
.isr_vector     0x02120004      0x13c
.text           0x02120140    0x10000 boards/unhygienix/tuya/src/main.c.1.o
""" + self.LAYOUT.replace("0x28014000 _heap_start", "0x2802b000 _heap_start")
        errors = MODULE.validate_map(text)
        self.assertTrue(any("isr_vector" in error for error in errors))
        self.assertTrue(any("stack/heap" in error for error in errors))
        self.assertTrue(any("vendor/reference" in error for error in errors))

    def test_flash_images_contain_firmware_resources_and_formatted_pfs(self):
        firmware = b"firmware"
        resources = b"resources"
        protected = build_pebble_flash_image(firmware, resources)
        protected_prefix_size = (
            (PEBBLE_RESOURCES_LOGICAL_OFFSET + PEBBLE_RESOURCES_SIZE) * 34 // 32
        )
        self.assertEqual(len(protected), protected_prefix_size)
        logical = verify_protected_blocks(protected)

        self.assertEqual(logical[: len(firmware)], firmware)
        resource_start = PEBBLE_RESOURCES_LOGICAL_OFFSET
        self.assertEqual(
            logical[resource_start : resource_start + len(resources)], resources
        )
        self.assertGreaterEqual(
            PEBBLE_FILESYSTEM_PHYSICAL_OFFSET,
            PEBBLE_AP_PHYSICAL_OFFSET + len(protected),
        )
        pfs = build_empty_pfs()
        self.assertEqual(len(pfs), PEBBLE_FILESYSTEM_SIZE)
        expected_prefix = (
            PFS_VERSION.to_bytes(2, "little")
            + b"\xff"
            + bytes([PFS_ERASED_PAGE_FLAGS])
            + b"\xff" * 4
            + PFS_INITIAL_ERASE_COUNT.to_bytes(4, "little")
        )
        self.assertEqual(pfs[: len(expected_prefix)], expected_prefix)
        self.assertEqual(
            pfs[PFS_PAGE_SIZE : PFS_PAGE_SIZE + len(expected_prefix)], expected_prefix
        )
        self.assertEqual(
            pfs[len(expected_prefix) : PFS_PAGE_SIZE],
            b"\xff" * (PFS_PAGE_SIZE - len(expected_prefix)),
        )

    def test_flash_image_rejects_region_overflow(self):
        with self.assertRaises(ValueError):
            build_pebble_flash_image(b"x" * (PEBBLE_RESOURCES_LOGICAL_OFFSET + 1), b"")
        with self.assertRaises(ValueError):
            build_pebble_flash_image(b"", b"x" * (0x100000 + 1))


if __name__ == "__main__":
    unittest.main()
