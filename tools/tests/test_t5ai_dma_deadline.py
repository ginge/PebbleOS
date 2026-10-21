# SPDX-License-Identifier: Apache-2.0
"""Exercise actual DMA finish code with elapsed, preempted and wrapped time."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define DISPLAY_DMA_ENABLE 1u
#define DISPLAY_DMA_STATUS_BUS_ERROR (1u<<20)
#define DISPLAY_DMA_TIMEOUT_TICKS 640u
#define TRANSFER_BEGIN(x)
static uint32_t DMA_CTRL,DMA_STATUS,s_dma_started_ticks,ticks,step,finish_at;
static bool s_dma_pending;
static unsigned reads;
static uint32_t now(void) {
 ++reads;ticks+=step;
 if(finish_at && (uint32_t)(ticks-s_dma_started_ticks)>=finish_at)DMA_CTRL=0;
 return ticks;
}
#define T5AI_AON_RTC_COUNT_LOW now()
"""
MAIN = r"""
static bool run(uint32_t start,uint32_t advance,uint32_t finish,uint32_t age,unsigned error) {
 s_dma_started_ticks=start;ticks=start+age;step=advance;finish_at=finish;reads=0;
 DMA_CTRL=1;DMA_STATUS=error;s_dma_pending=true;
 size_t aperture=4;bool done=prv_pixel_stream_finish_dma(&aperture,932);
 assert(!DMA_CTRL && !s_dma_pending && reads<1000);
 assert(aperture==(done?237u:4u));return done;
}
int main(void) {
 assert(run(0,1,10,0,0));
 assert(run(UINT32_MAX-5,1,10,0,0));
 assert(!run(0,1,0,0,0));
 assert(!run(UINT32_MAX-5,1,0,0,0));
 assert(!run(0,1,0,700,0));assert(reads==1);
 assert(!run(0,1,10,0,1u<<20));
 // A preemption can jump past the deadline; it must not restart the budget.
 assert(!run(0,1000,0,0,0));assert(reads==1);
 // Completion observed after preemption is successful when no transfer remains active.
 assert(run(0,1000,10,0,0));
 s_dma_pending=false;size_t a=7;assert(prv_pixel_stream_finish_dma(&a,932) && a==7);
 return 0;
}
"""


class DmaDeadlineTest(unittest.TestCase):
    def test_wall_time_wrap_errors_and_preemption(self):
        s = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()
        start = s.index("static bool prv_pixel_stream_finish_dma(")
        end = s.index("\nstatic size_t s_dma_pending_bytes;", start)
        source = re.sub(
            r"#ifdef CONFIG_T5AI_DISPLAY_PROFILE.*?#endif",
            "",
            s[start:end],
            flags=re.DOTALL,
        )
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(PREFIX + source + MAIN)
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
