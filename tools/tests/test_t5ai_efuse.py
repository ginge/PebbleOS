# SPDX-License-Identifier: Apache-2.0
"""Execute the serial reader with bounded MMIO completion and stale-data models."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class EfuseTest(unittest.TestCase):
    def test_completion_validity_and_serial_contract(self):
        source = (ROOT / 'drivers/t5ai/mcu.c').read_text()
        defines = '\n'.join(line for line in source.splitlines() if line.startswith('#define ') and 'volatile' not in line)
        functions = source[source.index('static bool prv_read_byte'):source.index('uint32_t mcu_cycles')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef int StatusCode;
enum {S_SUCCESS, E_INVALID_ARGUMENT, E_OUT_OF_MEMORY, E_ERROR};
static uint32_t ctrl, clock_reg, global_reg, polls, busy, valid, optr_reads, mask;
static unsigned address, serial_mode;
static uint32_t *ctrl_reg(void) {
  if (ctrl & 1u) {
    ++polls;
    if (polls > busy) {ctrl &= ~1u; polls=0;}
  }
  address=(ctrl>>8)&31u;
  return &ctrl;
}
static uint32_t optr(void) {
  assert(!(ctrl&1u)); ++optr_reads;
  return valid | (serial_mode ? (0x80u+address) : 0xabu);
}
#define T5AI_EFUSE_CTRL (*ctrl_reg())
#define T5AI_EFUSE_OPTR optr()
#define T5AI_SYSTEM_DEVICE_CLOCK_ENABLE clock_reg
#define T5AI_EFUSE_GLOBAL_CTRL global_reg
static uint32_t __get_PRIMASK(void){return mask;}
static void __disable_irq(void){mask=1;}
static void __enable_irq(void){mask=0;}
'''
        main = r'''
int main(void) {
 for(unsigned valid_flag=0;valid_flag<2;++valid_flag) {
  unsigned delays[]={UINT32_MAX,T5AI_EFUSE_READ_TIMEOUT+1,
                     T5AI_EFUSE_READ_TIMEOUT,T5AI_EFUSE_READ_TIMEOUT-1,1,0};
  for(unsigned i=0;i<6;++i) {
   ctrl=0;polls=0;optr_reads=0;busy=delays[i];valid=valid_flag?T5AI_EFUSE_READ_VALID:0;
   uint8_t value=0x55;
   bool ok=prv_read_byte(0x18,&value);
   assert(ok==(valid_flag && busy<=T5AI_EFUSE_READ_TIMEOUT));
   assert(value==(ok?0xab:0x55));
   assert(optr_reads==(busy<=T5AI_EFUSE_READ_TIMEOUT));
   assert(polls<=T5AI_EFUSE_READ_TIMEOUT+1);
  }
 }
 uint8_t bytes[7];memset(bytes,0x55,sizeof(bytes));size_t size=4;
 assert(mcu_get_serial(NULL,&size)==E_INVALID_ARGUMENT);
 assert(mcu_get_serial(bytes,NULL)==E_INVALID_ARGUMENT);
 assert(mcu_get_serial(bytes,&size)==E_OUT_OF_MEMORY && size==5);
 for(unsigned inherited=0;inherited<2;++inherited) {
  ctrl=0;polls=0;busy=0;valid=T5AI_EFUSE_READ_VALID;serial_mode=1;
  mask=inherited;clock_reg=inherited?T5AI_EFUSE_CLOCK_ENABLE:0;size=7;
  assert(mcu_get_serial(bytes,&size)==S_SUCCESS && size==5);
  for(unsigned i=0;i<5;++i)assert(bytes[i]==0x98+i);
  assert(bytes[5]==0x55 && mask==inherited);
  assert(clock_reg==(inherited?T5AI_EFUSE_CLOCK_ENABLE:0));
 }
 ctrl=0;polls=0;busy=UINT32_MAX;size=7;clock_reg=0;mask=0;
 assert(mcu_get_serial(bytes,&size)==E_ERROR && size==7 && mask==0 && clock_reg==0);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p/'test.c').write_text(harness+defines+'\n'+functions+main)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
