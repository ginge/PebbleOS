# SPDX-License-Identifier: Apache-2.0
"""Compile the actual driver memory helpers and verify offsets, tails and guards."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = "#include <assert.h>\n#include <stdint.h>\n#include <string.h>\n#include SOURCE\nint main(void) {\n  _Alignas(16) uint8_t src[1024], actual[1024], expected[1024];\n  for (unsigned i=0;i<sizeof(src);++i) src[i]=(i*137u)^31u;\n  for(unsigned a=0;a<8;++a) for(unsigned b=0;b<8;++b)\n    for(unsigned n=0;n<=960;++n) {\n      memset(actual,0x5a,sizeof actual);memcpy(expected,actual,sizeof actual);\n      memcpy(expected+a,src+b,n);t5_row_copy(actual+a,src+b,n);\n      assert(!memcmp(actual,expected,sizeof actual));\n      for(unsigned v=0;v<256;v+=17) {\n        memset(actual,0xa5,sizeof actual);memcpy(expected,actual,sizeof actual);\n        memset(expected+a,v,n);t5_row_fill(actual+a,v,n);\n        assert(!memcmp(actual,expected,sizeof actual));\n      }\n    }\n  return 0;\n}\n"


class RowMemoryTest(unittest.TestCase):
    def test_copy_fill(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-I",
                    str(ROOT / "include"),
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    '-DSOURCE="'
                    + str(ROOT / "drivers/display/t5ai/row_memory.h")
                    + '"',
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
