# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TuyaFlashTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc"), "host C compiler required")
    def test_protected_address_translation(self):
        source = r'''
#include <assert.h>
#include <stdint.h>
#include <pbl/soc/t5ai/flash_translation.h>

int main(void) {
  static const struct { uint32_t logical, physical; } vectors[] = {
    {0, 0}, {31, 31}, {32, 34}, {63, 65}, {64, 68},
    {0x1000, 0x1100}, {0x10000, 0x11000},
  };
  for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
    uint32_t physical = 0, logical = 0;
    assert(t5ai_flash_logical_to_physical(vectors[i].logical, &physical));
    assert(physical == vectors[i].physical);
    assert(t5ai_flash_physical_to_logical(physical, &logical));
    assert(logical == vectors[i].logical);
  }
  uint32_t value;
  assert(!t5ai_flash_physical_to_logical(32, &value));
  assert(!t5ai_flash_physical_to_logical(33, &value));
  assert(t5ai_flash_physical_to_logical(34, &value) && value == 32);
  assert(!t5ai_flash_logical_to_physical(UINT32_MAX, &value));
  assert(!t5ai_flash_logical_to_physical(0, 0));
  assert(!t5ai_flash_physical_to_logical(0, 0));
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test_source = directory / "test.c"
            test_source.write_text(source)
            binary = directory / "test"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "include"),
                    str(ROOT / "soc/t5ai/flash_translation.c"),
                    str(test_source), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_experimental_regions_do_not_overlap(self):
        header = (ROOT / "src/fw/flash_region/flash_region_t5ai.h").read_text()
        values = {}
        for line in header.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[0] == "#define" and fields[2].startswith("0x"):
                values[fields[1]] = int(fields[2], 16)

        regions = [
            "FIRMWARE_SLOT_0", "FILESYSTEM", "CD", "FIRMWARE_SLOT_1",
            "DEBUG_DB", "SHARED_PRF_STORAGE",
        ]
        spans = []
        for region in regions:
            begin = values[f"FLASH_REGION_{region}_BEGIN"]
            end = values[f"FLASH_REGION_{region}_END"]
            self.assertLess(begin, end, region)
            self.assertEqual(begin % 0x1000, 0, region)
            self.assertEqual(end % 0x1000, 0, region)
            spans.append((begin, end, region))

        for (_, left_end, left), (right_begin, _, right) in zip(spans, spans[1:]):
            self.assertLessEqual(left_end, right_begin, f"{left} overlaps {right}")


if __name__ == "__main__":
    unittest.main()
