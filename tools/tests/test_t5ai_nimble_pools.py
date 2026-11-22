# SPDX-License-Identifier: Apache-2.0
"""Exercise real linker placement and reject upstream pool drift before packaging."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.check_unhygienix_image_map import validate_map, validate_nimble_pools
from tools.tests import test_unhygienix_image as image_tests

ROOT = Path(__file__).resolve().parents[2]


class NimblePoolPlacementTest(unittest.TestCase):
    def test_map_rejects_missing_renamed_duplicate_resized_or_misplaced_inputs(self):
        base = (
            "FLASH 0x02120000 0x001a0000 xr\n.isr_vector 0x02120000 0x140\n"
            + image_tests.UnhygienixImageMapTest.LAYOUT
        )
        self.assertEqual(validate_map(base), [])
        for fault in (
            base.replace(".bss.os_msys_1_data", ".bss.upstream_msys_data"),
            base.replace(".bss.pool_acl_buf", ".bss.upstream_acl_data"),
            base.replace("0x27ec /tmp", "0x27f0 /tmp"),
            base.replace("0x2580 nimble", "0x257c nimble"),
            base.replace("0x6001300c 0x2580", "0x2801300c 0x2580"),
            base.replace("0x6001300c 0x2580", "0x60013008 0x2580"),
            base.replace(
                "0x6001300c __t5ai_acl_pool_start__",
                "0x60013010 __t5ai_acl_pool_start__",
            ),
            base.replace(
                "0x600155a0 __t5ai_board_cache_end__",
                "0x6001559c __t5ai_board_cache_end__",
            ),
            base.replace(
                "0x6001081e __t5ai_framebuffer_end__",
                "0x60010824 __t5ai_framebuffer_end__",
            ),
            base.replace("0x60010820 0x4d80", "0x60010820 0x20000"),
            base + " .bss.pool_acl_buf 0x60018000 0x2580 duplicate.o\n",
            base.replace(".t5ai_board_cache\n", ".renamed_cache\n"),
            base.replace("__t5ai_msys_pool_end__", "__removed_pool_end__"),
        ):
            with self.subTest(map_text=fault):
                self.assertTrue(any("NimBLE" in error for error in validate_map(fault)))
        self.assertEqual(
            validate_map(
                base.replace("nimble.a(transport.c.o)", "nimble.a(transport.c.obj)")
            ),
            [],
        )
        # GNU ld can wrap either input or output section names.
        self.assertEqual(
            validate_map(
                base.replace(
                    ".t5ai_board_cache\n                ", ".t5ai_board_cache "
                )
            ),
            [],
        )
        self.assertEqual(
            validate_map(
                base.replace(
                    ".bss.pool_acl_buf ", ".bss.pool_acl_buf\n                "
                )
            ),
            [],
        )

    def test_real_linker_fragment_fails_pool_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            fragment = subprocess.check_output(
                ["cc", "-E", "-P", "-x", "c", str(ROOT / "soc/t5ai/framebuffer.ld")],
                text=True,
            )
            (tmp / "layout.ld").write_text(
                "MEMORY {\n"
                "T5AI_DISPLAY_RAM (rw) : ORIGIN = 0x60000000, LENGTH = 0x20000\n"
                "T5AI_DISPLAY_DMA_RAM (rw) : ORIGIN = 0x60024000, LENGTH = 0x4000\n"
                "}\nSECTIONS {\n" + fragment + "\n}\n"
            )
            for fault in (
                "",
                "rename-msys",
                "rename-acl",
                "grow-msys",
                "shrink-acl",
                "duplicate-msys",
            ):
                with self.subTest(fault=fault):
                    msys = (
                        "upstream_msys_data"
                        if fault == "rename-msys"
                        else "os_msys_1_data"
                    )
                    acl = (
                        "upstream_acl_data" if fault == "rename-acl" else "pool_acl_buf"
                    )
                    msys_size = 10224 if fault == "grow-msys" else 10220
                    acl_size = 9596 if fault == "shrink-acl" else 9600
                    source = (
                        'static unsigned char fb[67616] __attribute__((used,section(".t5ai_framebuffer"),aligned(32)));\n'
                        f"static unsigned char {msys}[{msys_size}] __attribute__((used,aligned(4)));\n"
                        f"static unsigned char {acl}[{acl_size}] __attribute__((used,aligned(4)));\n"
                    )
                    (tmp / "pools.c").write_text(source)
                    subprocess.run(
                        [
                            "cc",
                            "-c",
                            "-fdata-sections",
                            "-fno-common",
                            str(tmp / "pools.c"),
                            "-o",
                            str(tmp / "pools.o"),
                        ],
                        check=True,
                        capture_output=True,
                    )
                    objects = [str(tmp / "pools.o")]
                    if fault == "duplicate-msys":
                        objects.append(str(tmp / "pools.o"))
                    result = subprocess.run(
                        [
                            "ld",
                            "--gc-sections",
                            "-T",
                            str(tmp / "layout.ld"),
                            "-Map",
                            str(tmp / "image.map"),
                            "-o",
                            str(tmp / "image.elf"),
                            *objects,
                        ],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    if fault:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("T5AI NimBLE", result.stderr)
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(
                            validate_nimble_pools((tmp / "image.map").read_text()), []
                        )
