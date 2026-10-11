# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual RAM SPI source with FIFO ordering and injected failures."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
static uint32_t spi[8],sys[13],now,tx,rx,expected,depth,writes;
static uint8_t bytes[8192];
static int fault;
uint32_t stock_irq_disable(void){assert(!depth);depth=1;return 7;}
void stock_irq_enable(uint32_t f){assert(f==7 && depth);depth=0;}
static uint32_t read_reg(uint32_t a){
 if(a==0x4400020c)return now++;
 if(a>=SYS && a<SYS+sizeof(sys))return sys[(a-SYS)/4];
 assert(a>=SPI && a<SPI+sizeof(spi));
 if(a==SPI+24){
  if(fault==2)return 0;
  uint32_t v=tx-rx<16?2:0;
  if(spi[5]&1){
   if(fault==3)return v;
   if(fault==1)return v|0x800;
   if(fault==4)return v|0x1000;
   if(rx<tx)v|=4;
   if(tx==expected)v|=0x2000;
  }
  return v;
 }
 if(a==SPI+28){assert((spi[5]&1) && rx<tx);++rx;return 0;}
 return spi[(a-SPI)/4];
}
static void write_reg(uint32_t a,uint32_t v){
 if(a>=SYS && a<SYS+sizeof(sys)){
  if(a==SYS+8 && !(v&512))assert(!spi[5]);
  sys[(a-SYS)/4]=v;return;
 }
 assert(a>=SPI && a<SPI+sizeof(spi));
 if(a==SPI+16 && v && v!=spi[4])assert(!(spi[4]&(1u<<23)));
 if(a==SPI+24){if(v&0x30000){assert(!spi[5]);tx=rx=expected=0;}return;}
 if(a==SPI+28){assert(depth && (sys[2]&512));assert(tx-rx<16);bytes[writes++]=v;++tx;return;}
 if(a==SPI+20 && (v&1)){assert(tx>0);expected=(v>>8)&4095;assert(!(v&2) && !((v>>20)&4095));}
 spi[(a-SPI)/4]=v;
}
static void setup(void){
 for(unsigned i=0;i<8;++i)spi[i]=0;
 for(unsigned i=0;i<13;++i)sys[i]=0;
 spi[2]=3;spi[4]=0xc20100;sys[10]=0x19;sys[12]=0x10;
 now=UINT32_MAX-5;tx=rx=expected=depth=writes=0;fault=0;
}
static void restored(void){assert(!depth && !spi[5] && spi[2]==3 && spi[4]==0xc20100 && sys[2]==0 && sys[10]==0x19 && sys[12]==0x10);}
int main(void){
 uint8_t prefix=0x50,data[40]={1,0};
 setup();assert(!pebble_spi_bypass_write(&prefix,data,2));restored();
 assert(writes==3 && bytes[0]==0x50 && bytes[1]==1 && bytes[2]==0);
 setup();assert(!pebble_spi_bypass_write(0,data,40));restored();assert(writes==40);
 for(int f=1;f<=4;++f){setup();fault=f;assert(pebble_spi_bypass_write(&prefix,data,2)==(f==2?-2:f==3?-1:-3));restored();}
 setup();assert(pebble_spi_bypass_write(0,0,1)==-4 && !depth && !writes);
 setup();assert(pebble_spi_bypass_write(0,data,4096)==-4 && !depth && !writes);
 setup();spi[5]=1;assert(pebble_spi_bypass_write(0,data,1)==-5 && !depth && !writes);
}
"""


class CPSPITest(unittest.TestCase):
    def test_fifo_preload_completion_wrap_deadline_and_restore(self):
        source = (ROOT / "boards/unhygienix/tuya/cp/flash_spi.c").read_text()
        source = re.sub(r"R\(([^;\n]+)\) = ([^;\n]+);", r"write_reg(\1, \2);", source)
        source = source.replace(
            "#define R(a) (*(volatile uint32_t *)(uintptr_t)(a))",
            "#define R(a) read_reg(a)\nstatic uint32_t read_reg(uint32_t);\nstatic void write_reg(uint32_t,uint32_t);",
        )
        source = source.replace('__asm__ volatile("dsb; isb" ::: "memory");', "")
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
                    "-Werror",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True, timeout=5)
