# SPDX-License-Identifier: Apache-2.0
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r"""
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
static unsigned depth,logs,expected;
static bool check_serial;
#ifdef CONFIG_T5AI_DRAW_PROFILE
static unsigned sleeps,interrupt_after;
static bool contact;
static void pbl_irq_lock(void){assert(depth<2);++depth;}
static void pbl_irq_unlock(void){assert(depth);--depth;}
static bool t5ai_draw_profile_gesture_active(void){return contact;}
static void pbl_thread_sleep(unsigned ms){assert(!depth && ms==2);++sleeps;if(sleeps==interrupt_after)contact=true;}
#define PBL_MSEC(ms) ms
#endif
static void debug(const char *format,...){
 assert(!depth);if(!strncmp(format,"T5BUFFER",8))return;
 ++logs;
 if(check_serial && !strncmp(format,"T5CADENCE",9)) {
  va_list args;va_start(args,format);assert(va_arg(args,unsigned long)==expected++);
  for(unsigned i=1;i<5;++i)assert(va_arg(args,unsigned long)==i+1);
  va_end(args);
 }
}
#define PBL_LOG_DBG(...) debug(__VA_ARGS__)
"""
MAIN = r"""
int main(void){
 uint32_t a[7]={1,2,3,4,5,6,7};
#ifdef CONFIG_T5AI_DRAW_PROFILE
 memset(s_records,0xaa,sizeof s_records);
 prv_record(Log_T5CADENCE,a,5);assert(!logs && s_log_startup==1);
 t5ai_display_profile_buffer_ready();
 for(unsigned i=0;i<LOG_CAPACITY+9;++i){a[0]=i;prv_record(Log_T5CADENCE,a,5);}
 assert(s_log_count==LOG_CAPACITY && s_log_dropped==9 && !logs);
 expected=9;check_serial=true;t5ai_display_profile_dump();
 assert(logs==LOG_CAPACITY && sleeps==LOG_CAPACITY && !s_log_count && expected==LOG_CAPACITY+9);
 t5ai_display_profile_buffer_ready();
 for(unsigned i=0;i<8;++i){a[0]=i;prv_record(Log_T5CADENCE,a,5);}
 expected=0;interrupt_after=sleeps+3;t5ai_display_profile_dump();assert(s_log_count==5 && contact);
 contact=false;interrupt_after=0;t5ai_display_profile_dump();assert(!s_log_count && expected==8);
 check_serial=false;
 unsigned before=logs;
 for(unsigned i=0;i<Log_Count;++i)prv_record(i,a,7);
 t5ai_display_profile_dump();assert(logs==before+Log_Count);
 prv_record(Log_Count,a,7);prv_record(Log_T5CADENCE,a,8);assert(!s_log_count);
#else
 a[0]=4;expected=4;check_serial=true;prv_record(Log_T5CADENCE,a,5);assert(logs==1 && expected==5);
#endif
 return 0;
}
"""


class DisplayBufferTest(unittest.TestCase):
    def test_order_overflow_poison_gesture_pause_and_immediate_mode(self):
        source = (ROOT / "soc/t5ai/display_profile.c").read_text()
        source = source[
            source.index("typedef enum {") : source.index("#define PROFILE_FRAMES")
        ]
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(PREFIX + source + MAIN)
            for flags in [
                [],
                ["-DCONFIG_T5AI_DRAW_PROFILE"],
                ["-DCONFIG_T5AI_DRAW_PROFILE", "-DCONFIG_T5AI_DRAW_PROFILE_DETAIL"],
            ]:
                subprocess.run(
                    [
                        "cc",
                        "-O2",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-fsanitize=undefined,bounds",
                        "-fno-sanitize-recover=all",
                        *flags,
                        str(p / "test.c"),
                        "-o",
                        str(p / "test"),
                    ],
                    check=True,
                )
                subprocess.run([str(p / "test")], check=True)
