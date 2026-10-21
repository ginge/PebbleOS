# SPDX-License-Identifier: Apache-2.0
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests import test_t5ai_display_rotation as t

ROOT = Path(__file__).resolve().parents[2]
s = (ROOT / "drivers/display/t5ai/display_co5300.c").read_text()


def fn(name):
    m = re.search(r"\b" + name + r"\(", s)
    start = s.rfind("\nstatic ", 0, m.start()) + 1
    a = s.index("{", m.end())
    b = a + 1
    d = 1
    while d:
        d += (s[b] == "{") - (s[b] == "}")
        b += 1
    return s[start:b]


prefix = t.PREFIX.replace(
    "#define TRANSFER_BEGIN(name)",
    """#define CONFIG_T5AI_DISPLAY_RECOVERY 1
#define T5AI_AON_RTC_HZ 32000u
static uint32_t ticks;
#define PBL_MSEC(x) (x)
static void pbl_thread_sleep(unsigned ms) {assert(ms==1);ticks+=32000u;}
#define T5AI_AON_RTC_COUNT_LOW ticks
#define TRANSFER_BEGIN(name)""",
)
prefix = prefix.replace(
    "static size_t pending_length,cursor,window_start;",
    """static size_t pending_length,cursor,window_start;
static unsigned fail_at;
static DisplayRow s_replay_rows[260];
static unsigned s_replay_count,s_replay_next;
static uint32_t s_dma_retry_at,s_dma_retry_count,s_dma_errors,s_dma_recoveries;
static bool test_ok=true;
static void prv_dma_init(void) {assert(!pending);}
static bool prv_dma_self_test(void) {assert(!pending);return test_ok;}
""",
)
prefix = prefix.replace(
    "if(!pending)return true;",
    "if(!pending)return true;\n if(fail_at && submits==fail_at){pending=NULL;fail_at=0;return false;}",
)
names = [
    "prv_dma_failed",
    "prv_replay_next_row",
    "prv_init_conversion_tables",
    "prv_convert_row",
    "prv_flush_dma_batch",
    "prv_finish_row_dma",
    "prv_send_logical_row",
    "prv_replay_dma_row",
    "prv_recover_dma_frame",
    "prv_stream_logical_row",
    "prv_profile_next_row",
    "prv_display_update",
]
main = t.MAIN.replace(
    "s_rotated=rotated;",
    "s_dma_retry_count=0;s_dma_display_enabled=true;s_rotated=rotated;",
)
main = main.replace(" && !s_dma_display_failed", "")
# Each run fails an early batch when DMA is actually used; check exact repaired pixels.
main = main.replace("pending=NULL;cursor=", "fail_at=dma?2:0;pending=NULL;cursor=")
main = main.replace(" for(unsigned r=0;", " for(unsigned r=0;")
main = main.replace(
    " return 0;",
    """
 // Empty frames still service bounded retry; elapsed arithmetic must wrap.
 count=next=0;queued=false;s_updating=false;s_rotated=false;
 s_dma_display_failed=true;s_dma_display_enabled=false;s_dma_retry_count=0;
 s_dma_retry_at=UINT32_MAX-100;ticks=s_dma_retry_at+31999u;
 unsigned recovered=s_dma_recoveries;
 prv_display_update(source,done,true,false,false);assert(s_dma_retry_count==1);
 ++ticks;prv_display_update(source,done,true,false,false);
 assert(!s_dma_display_failed && s_dma_recoveries==recovered+1 && s_dma_retry_count==1);
 s_dma_display_failed=true;s_dma_display_enabled=false;test_ok=false;
 s_dma_retry_count=0;s_dma_retry_at=ticks;
 for(unsigned i=0;i<5;++i){ticks+=32000;prv_display_update(source,done,true,false,false);}
 assert(s_dma_retry_count==3 && s_dma_display_failed);
 ticks+=32000;prv_display_update(source,done,false,false,false);assert(s_dma_retry_count==3);
 return 0;""",
)


class RecoveryTest(unittest.TestCase):
    def test_repaint_wrap_and_retry_budget(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.check_configs(directory)

    def check_configs(self, directory):
        for rot in [False, True]:
            extra = fn("prv_rotated_next_row") if rot else ""
            for batch in [1, 2, 3]:
                (directory / "test.c").write_text(
                    prefix + extra + "\n".join(fn(n) for n in names) + main
                )
                cmd = [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    "-I" + str(ROOT / "include"),
                    f"-DCONFIG_T5AI_DISPLAY_BATCH_ROWS={batch}",
                ]
                if rot:
                    cmd += ["-DCONFIG_T5AI_DISPLAY_ROTATED_DMA"]
                subprocess.run(
                    cmd + [str(directory / "test.c"), "-o", str(directory / "test")],
                    check=True,
                )
                subprocess.run([str(directory / "test")], check=True)
