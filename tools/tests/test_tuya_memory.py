# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
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

    def test_pfs_flags_reservation_stays_in_vendor_ap_audio_slab(self):
        layout = ROOT / ("third_party/hal_tuya/TuyaOpen-T5AI/t5_os/projects/"
                         "tuya_app/partitions/bk7258/ram_regions.csv")
        regions = {}
        cursor = 0
        for line in layout.read_text().splitlines():
            fields = [field.strip() for field in line.split(",")]
            if len(fields) != 4 or fields[1] != "PSRAM":
                continue
            start = int(fields[2], 0) if fields[2] else cursor
            cursor = start + int(fields[3], 0)
            regions[fields[0]] = (start, cursor)
        header = (ROOT / "include/pbl/soc/t5ai/memory.h").read_text()
        def constant(name):
            return int(re.search(r"#define " + name + r" (0x[0-9a-fA-F]+|[0-9]+)u",
                                 header).group(1), 0)
        begin = constant("T5AI_PFS_FLAGS_PSRAM_BASE")
        end = begin + constant("T5AI_PFS_FLAGS_PSRAM_SIZE")
        audio_begin, audio_end = regions["PSRAM_MEM_SLAB_AUDIO"]
        self.assertGreaterEqual(begin, audio_begin)
        self.assertLessEqual(end, audio_end)
        self.assertEqual(regions["CP_PSRAM_HEAP"][0], 0x60700000)
        self.assertLessEqual(end, regions["CP_PSRAM_HEAP"][0])
        self.assertNotEqual(constant("T5AI_PFS_FLAGS_MPU_REGION"),
                            constant("T5AI_DISPLAY_PSRAM_MPU_REGION"))

    def test_ap_only_nimble_pool_storage_uses_t5ai_psram(self):
        sections = (ROOT / "soc/t5ai/framebuffer.ld").read_text()
        self.assertIn("KEEP(*(.bss.os_msys_1_data))", sections)
        self.assertIn("KEEP(*(.bss.pool_acl_buf))", sections)
        self.assertIn("AP-only NimBLE pool backing arrays", sections)

    def test_put_bytes_pool_bounds_clearing_and_slot_isolation(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("Host C compiler unavailable")
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "pbl/services/put_bytes/put_bytes_job_buffer.h"
int main(void) {
  uint8_t *slots[MAX_BATCHED_PB_PUT_OPS];
  for (size_t i = 0; i < MAX_BATCHED_PB_PUT_OPS; ++i) {
    slots[i] = put_bytes_job_buffer_alloc(i, PUT_BYTES_JOB_BUFFER_CAPACITY);
    assert(slots[i] && (uintptr_t)slots[i] % 32 == 0);
    for (size_t j = 0; j < i; ++j) assert(slots[i] != slots[j]);
    memset(slots[i], i + 1, PUT_BYTES_JOB_BUFFER_CAPACITY);
  }
  assert(!put_bytes_job_buffer_alloc(MAX_BATCHED_PB_PUT_OPS, 1));
  assert(!put_bytes_job_buffer_alloc(SIZE_MAX, 1));
  assert(!put_bytes_job_buffer_alloc(0, PUT_BYTES_JOB_BUFFER_CAPACITY + 1));
  assert(!put_bytes_job_buffer_alloc(0, SIZE_MAX));
  put_bytes_job_buffer_free(MAX_BATCHED_PB_PUT_OPS, slots[0]);
  put_bytes_job_buffer_free(0, NULL);
  put_bytes_job_buffer_free(0, slots[1]);
  for (size_t i = 0; i < MAX_BATCHED_PB_PUT_OPS; ++i) {
    for (size_t j = 0; j < PUT_BYTES_JOB_BUFFER_CAPACITY; ++j)
      assert(slots[i][j] == i + 1);
  }
  for (size_t i = 0; i < MAX_BATCHED_PB_PUT_OPS; ++i) {
    put_bytes_job_buffer_free(i, slots[i]);
    for (size_t j = 0; j < PUT_BYTES_JOB_BUFFER_CAPACITY; ++j)
      assert(slots[i][j] == 0);
    memset(slots[i], 0x5a, PUT_BYTES_JOB_BUFFER_CAPACITY);
    assert(put_bytes_job_buffer_alloc(i, 17) == slots[i]);
    for (size_t j = 0; j < PUT_BYTES_JOB_BUFFER_CAPACITY; ++j)
      assert(slots[i][j] == (j < 17 ? 0 : 0x5a));
  }
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "put_bytes_pool.c"
            binary = Path(directory) / "put_bytes_pool"
            source.write_text(harness)
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "include"), str(source),
                 str(ROOT / "drivers/t5ai/put_bytes_job_buffer.c"),
                 "-o", str(binary)], check=True,
            )
            subprocess.run([str(binary)], check=True)

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
