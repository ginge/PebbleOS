# SPDX-License-Identifier: Apache-2.0
"""Execute the actual volatile BP hook with controller, SPI and fault injection."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <setjmp.h>
static uint32_t chip, cached, ctl[32], now, actual;
static unsigned prepares, finishes, depth, writes, fail_spi, bad_readback, bad_restore, busy;
static jmp_buf reset;
void bk_printf(const char *fmt, ...) {(void)fmt;}
static uint32_t *reg(uintptr_t a) {
 if(a==0x2806a734) return &chip;
 if(a==0x2806a738) return &cached;
 static uint32_t base=0x44030000;
 if(a==0x2806a72c) return &base;
 assert(a>=base && a<base+sizeof(ctl)); return &ctl[(a-base)/4];
}
uint32_t pebble_flash_wait(uint32_t base) {assert(base==0x44030000);return 0;}
uint32_t pebble_flash_launch(uint32_t base) {
 assert(base==0x44030000 && depth==1 && prepares==finishes+1);
 unsigned cmd=(ctl[84/4]>>24)&31;
 assert(cmd==3 || cmd==6);
 ctl[36/4]=cmd==3 ? (actual &255)|(busy ? 1:0) : actual>>8;
 return 0;
}
int pebble_flash_prepare(void) {assert(!depth);++prepares;return 0;}
int pebble_flash_finish(void) {assert(!depth);++finishes;return 0;}
uint32_t pebble_flash_irq_enter(void) {assert(!depth);++depth;return 7;}
void pebble_flash_irq_exit(uint32_t flags) {assert(flags==7 && depth==1);--depth;}
void pebble_flash_fatal(void) {longjmp(reset,1);}
void stock_status(uint32_t requested) {
 if(!bad_restore) actual=requested;
 else if(bad_restore==2) actual=requested|2;
 cached=requested;
}
int stock_bypass_write(uint8_t *enable,uint8_t *payload,uint32_t size) {
 assert(depth==1 && *enable==0x50 && size==2 && payload[0]==1);
 assert(!(payload[1]&3));++writes;
 if(bad_readback==3) actual=((actual&0xff00)|payload[1])|2;
 else if(bad_readback==2) actual=0x838;
 else if(!bad_readback) actual=(actual&0xff00)|payload[1];
 return fail_spi ? -1:0;
}
static void setup(void) {
 chip=0xc86517;cached=actual=0xa38;now=UINT32_MAX-10;
 prepares=finishes=depth=writes=fail_spi=bad_readback=bad_restore=busy=0;
}
int main(void) {
 setup();chip=0x123456;
 assert(!pebble_flash_volatile_status(0xa38,0xa00) && !prepares);
 setup();assert(!pebble_flash_volatile_status(0xa38,0x838) && !prepares);
 setup();assert(pebble_flash_volatile_status(0xa38,0xa00));
 assert(cached==0xa00 && actual==0xa00 && writes==1 && prepares==1 && finishes==1);
 assert(pebble_flash_volatile_status(0xa00,0xa38));
 assert(cached==0xa38 && actual==0xa38 && writes==1 && !depth);
 setup();fail_spi=1;
 assert(pebble_flash_volatile_status(0xa38,0xa00));
 assert(finishes==1 && cached==0xa00 && actual==0xa00);
 setup();bad_readback=1;
 assert(pebble_flash_volatile_status(0xa38,0xa00));
 assert(finishes==1 && cached==0xa00 && actual==0xa00);
 setup();bad_readback=1;bad_restore=1;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa38,0xa00);assert(0);}
 assert(!finishes && actual==0xa38);
 setup();bad_readback=2;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa38,0xa00);assert(0);}
 assert(!finishes && cached==0xa38 && actual==0x838);
 setup();actual=cached=0xa00;bad_restore=1;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa00,0xa38);assert(0);}
 assert(!finishes && actual==0xa00); /* Stock cache is not physical proof. */
 setup();bad_readback=3;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa38,0xa00);assert(0);}
 assert(!finishes && actual==0xa02); /* WEL is not a successful volatile postcondition. */
 setup();bad_readback=1;bad_restore=2;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa38,0xa00);assert(0);}
 assert(!finishes && actual==0xa02); /* Recovery must clear WEL too. */
 setup();actual=0x838;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa38,0xa00);assert(0);}
 assert(!writes && !finishes);
 setup();busy=1;
 if(!setjmp(reset)) {pebble_flash_volatile_status(0xa38,0xa00);assert(0);}
 assert(!writes && now<4000); /* wrap-safe independent deadline */
}
"""


class CPVolatileTest(unittest.TestCase):
    def test_sequence_readback_restoration_and_faults(self):
        source = (ROOT / "boards/unhygienix/tuya/cp/flash_volatile.c").read_text()
        source = source.replace(
            "#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))",
            "static uint32_t *reg(uintptr_t);\n#define REG32(a) (*reg(a))",
        )
        import re

        source = re.sub(
            r"#define NOW\s+REG32\(0x4400020c\)",
            "static uint32_t now;\n#define NOW (now++)",
            source,
        )
        source = source.replace("__attribute__((noreturn))", "")
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(source + HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-DPEBBLE_FLASH_VOLATILE_STATUS=1",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True, timeout=5)
