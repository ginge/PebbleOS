# SPDX-License-Identifier: Apache-2.0
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r"""
#include <stdint.h>
#include <assert.h>
#define T5IO_COUNT 13u
#define LOG_LEVEL_DEBUG 0
#define PBL_LOG_MODULE_DEFINE(...)
static unsigned depth,records;
static uint32_t output[T5IO_COUNT][4];
static void pbl_irq_lock(void){assert(depth<2);++depth;}
static void pbl_irq_unlock(void){assert(depth);--depth;}
static void log_sample(const char *fmt,unsigned i,unsigned long n,unsigned long t,unsigned long max,unsigned long amount){
 (void)fmt;assert(!depth && i<T5IO_COUNT);output[i][0]=n;output[i][1]=t;output[i][2]=max;output[i][3]=amount;++records;
}
#define PBL_LOG_DBG(...) log_sample(__VA_ARGS__)
"""
MAIN = r"""
int main(void){
 t5ai_io_profile_add(T5IO_COUNT,99,99);assert(!depth);
 pbl_irq_lock();t5ai_io_profile_add(3,7,1);assert(depth==1);pbl_irq_unlock();
 for(unsigned i=0;i<100;++i)t5ai_io_profile_add(2,i,10);
 t5ai_io_profile_add(7,UINT32_MAX,1);t5ai_io_profile_add(7,2,3);
 t5ai_io_profile_dump();assert(records==T5IO_COUNT && !depth);
 assert(output[2][0]==100 && output[2][1]==4950 && output[2][2]==99 && output[2][3]==1000);
 assert(output[7][0]==2 && output[7][1]==1 && output[7][2]==UINT32_MAX && output[7][3]==4);
 t5ai_io_profile_dump();assert(records==2*T5IO_COUNT);for(unsigned i=0;i<T5IO_COUNT;++i)for(unsigned j=0;j<4;++j)assert(!output[i][j]);
 return 0;
}
"""


class IoProfileTest(unittest.TestCase):
    def test_bounded_counters_reset_and_log_outside_irq_lock(self):
        source = (ROOT / "soc/t5ai/io_profile.c").read_text()
        source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.MULTILINE)
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
                    "-fsanitize=undefined,bounds",
                    "-fno-sanitize-recover=all",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
