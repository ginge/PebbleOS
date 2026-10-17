# SPDX-License-Identifier: Apache-2.0
import re
import shutil
import subprocess
import tempfile
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

    def test_framebuffer_psram_validation_detects_corruption_within_bounds(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        board = (ROOT / "fw/board/boards/board_unhygienix.c").read_text()
        start = board.index("static bool prv_validate_framebuffer_ram(void)")
        end = board.index("static void prv_assert_power_latch", start)
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static struct { uint32_t before, words[256], after; } memory;
#define __t5ai_framebuffer_start__ (memory.words)
#define __t5ai_framebuffer_end__ (memory.words + 256)
static unsigned barrier, corrupt_at;
static void dsb(void) {
  if (++barrier == corrupt_at) memory.words[137] ^= 1u;
}
#define __DSB() dsb()
""" + board[start:end] + r"""
int main(void) {
  for (corrupt_at = 0; corrupt_at <= 2; ++corrupt_at) {
    memory.before = 0x12345678u; memory.after = 0x87654321u; barrier = 0;
    assert(prv_validate_framebuffer_ram() == (corrupt_at == 0));
    assert(memory.before == 0x12345678u && memory.after == 0x87654321u);
    if (!corrupt_at) {
      for (unsigned i = 0; i < 256; ++i)
        assert(memory.words[i] == ~(0x3ca5c3a5u ^ i));
    }
  }
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "psram.c"
            binary = Path(directory) / "psram"
            source.write_text(harness)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
