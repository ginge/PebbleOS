# SPDX-License-Identifier: Apache-2.0
"""Inject only one genuinely completed transfer, with an AP-local wrapped delay."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests import test_t5ai_dma_deadline as deadline

ROOT = Path(__file__).resolve().parents[2]


class FaultInjectionTest(unittest.TestCase):
    def test_completed_only_once_after_elapsed_arming_delay(self):
        source = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
        begin = source.index("static bool prv_pixel_stream_finish_dma(")
        end = source.index("\nstatic size_t s_dma_pending_bytes;", begin)
        source = re.sub(
            r"#ifdef CONFIG_T5AI_DISPLAY_PROFILE.*?#endif",
            "",
            source[begin:end],
            flags=re.DOTALL,
        )
        prefix = (
            deadline.PREFIX
            + "\n#define CONFIG_T5AI_DISPLAY_FAULT_TEST\n#define T5AI_AON_RTC_HZ 32000u\nstatic uint32_t s_dma_fault_armed_at;\n"
        )
        run = deadline.MAIN.split("int main(void)")[0]
        main = r"""
int main(void) {
 s_dma_fault_armed_at=UINT32_MAX-100u;
 assert(!run(s_dma_fault_armed_at,1,0,0,0));
 assert(run(s_dma_fault_armed_at,1,10,0,0));
 uint32_t after=s_dma_fault_armed_at+8u*32000u+1u;
 assert(!run(after,1,10,0,0));
 assert(run(after+100,1,10,0,0));
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            p = Path(temporary)
            (p / "test.c").write_text(prefix + source + run + main)
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
