# SPDX-License-Identifier: Apache-2.0
"""Compile the T5AI ISR with a modeled receive FIFO, in both flag modes."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_i2c import API, HARNESS, ROOT

FIFO_HARNESS = r"""
static unsigned queued, pops, offset;
static uint8_t test_pop(void) {
 assert(queued && in_isr);
 --queued; ++pops;
 return (uint8_t)(offset++ ^ 0xa5u);
}
static void irq(const struct pbl_i2c_bus *bus, uint32_t status) {
 prv_regs(bus)->status=status;in_isr=true;pbl_i2c_t5ai_irq_handler(prv_dev(bus));in_isr=false;
}
static void begin(const struct pbl_i2c_bus *bus,uint8_t *buffer,unsigned size,bool reg) {
 elapsed=0;queued=pops=offset=0;
 bus->state->transfer=(struct pbl_i2c_transfer){.size=size,.data=buffer,.addr=0x6b,
  .reg=0x30,.dir=PBL_I2C_READ,
  .with_reg=reg};
 pbl_i2c_t5ai_ops.start_transfer(bus);
 assert(prv_regs(bus)->data==(reg?0xd6u:0xd7u));
 if(reg) {
  irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK);
  assert(prv_regs(bus)->data==0x30u);
  assert(prv_dev(bus)->state->phase==T5AI_I2C_RESTART);
  irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK);
  assert(prv_regs(bus)->data==0xd7u && prv_regs(bus)->status==T5AI_I2C_STATUS_START);
 }
 irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_START|T5AI_I2C_STATUS_ACK);
 assert(!pops);
}
static unsigned threshold(uint32_t control) {
 const unsigned n[]={12,8,4,1};return n[(control>>6)&3u];
}
static void data_irq(const struct pbl_i2c_bus *bus) {
 uint32_t control=prv_regs(bus)->status;
 queued=threshold(control);
 assert(queued<=12 && queued<=bus->state->transfer.size-prv_dev(bus)->state->index);
 irq(bus,control|T5AI_I2C_STATUS_INTERRUPT);
 assert(!queued);
}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0}; struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,
  .irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 uint8_t buffer[258];
 pbl_i2c_t5ai_ops.init(bus);pbl_i2c_t5ai_ops.enable(bus);
 for(unsigned reg=0;reg<2;++reg)for(unsigned size=1;size<=256;++size) {
  for(unsigned i=0;i<sizeof(buffer);++i)buffer[i]=0x7e;
  unsigned done=completions;
  begin(bus,buffer+1,size,reg);
  irq(bus,prv_regs(bus)->status); // No interrupt means no FIFO read.
  assert(!pops);
  unsigned entries=0;
  while(hw.index<size) {
   data_irq(bus);++entries;
   if(hw.index<size)assert(completions==done);
  }
  assert(completions==done+1 && state.event==PBL_I2C_EVENT_COMPLETE);
  assert(prv_regs(bus)->status==T5AI_I2C_STATUS_STOP && pops==size);
  assert(buffer[0]==0x7e && buffer[size+1]==0x7e);
  for(unsigned i=0;i<size;++i)assert(buffer[i+1]==(uint8_t)(i^0xa5u));
#ifdef CONFIG_T5AI_I2C_RX_FIFO
#ifdef CONFIG_T5AI_I2C_RX_FIFO_LIMIT4
  if(size==13)assert(entries==4);
  if(size==24)assert(entries==9);
#else
  if(size==9 || size==13)assert(entries==2);
  if(size==24)assert(entries==6);
#endif
#else
  assert(entries==size);
#endif
 }
 // Timeout/arbitration after partial data must not consume more bytes.
 const uint32_t errors[]={T5AI_I2C_STATUS_TIMEOUT,T5AI_I2C_STATUS_ARB_LOST};
 for(unsigned e=0;e<2;++e) {
  begin(bus,buffer,24,true);data_irq(bus);
  unsigned before=pops,done=completions,calls=gpio_calls;
  irq(bus,errors[e]|T5AI_I2C_STATUS_INTERRUPT);
  assert(completions==done+1 && state.event==PBL_I2C_EVENT_ERROR);
  assert(pops==before && gpio_calls==calls && hw.recovery_required && !irq_enabled);
  begin(bus,buffer,9,true);
  assert(!hw.recovery_required && irq_enabled);
  while(hw.index<9)data_irq(bus);
  assert(state.event==PBL_I2C_EVENT_COMPLETE);
 }
 // Read address NACK is not a data-ready interrupt.
 elapsed=0;state.transfer=(struct pbl_i2c_transfer){.size=9,.data=buffer,.addr=0x6b,
  .dir=PBL_I2C_READ,.with_reg=false};
 pbl_i2c_t5ai_ops.start_transfer(bus);unsigned before=pops,done=completions;
 irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_START);
 assert(completions==done+1 && pops==before && state.event==PBL_I2C_EVENT_ERROR);
 // A malformed level or stale interrupt cannot overrun the destination.
 begin(bus,buffer,2,false);before=pops;
#ifndef CONFIG_T5AI_I2C_RX_FIFO
 queued=1;
#endif
 irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK); // mode 0: twelve bytes
#ifdef CONFIG_T5AI_I2C_RX_FIFO
 assert(pops==before && state.event==PBL_I2C_EVENT_ERROR);
#else
 (void)before;
#endif
 // Zero-length requests do not issue START or touch the receive buffer.
 hw.recovery_required=false;state.transfer.size=0;elapsed=0;
 prv_regs(bus)->status=0x200u;before=pops;
 pbl_i2c_t5ai_ops.start_transfer(bus);
 assert(state.event==PBL_I2C_EVENT_ERROR && pops==before);
 assert(prv_regs(bus)->status==0x200u);
 return 0;
}
"""


class T5AII2CFifoTest(unittest.TestCase):
    def test_receive_fifo_and_byte_fallback(self):
        for enabled, limit4 in ((False, False), (True, False), (True, True)):
            with (
                self.subTest(fifo=enabled, limit4=limit4),
                tempfile.TemporaryDirectory() as temporary,
            ):
                path = Path(temporary)
                (path / "api.h").write_text(API)
                for name in (
                    "pbl/util/misc.h",

                    "pbl/drivers/i2c/t5ai.h",

                    "pbl/soc/t5ai/kernel.h",
                    "pbl/soc/t5ai/delay.h",
                    "system/passert.h",
                    "cmsis_core.h",
                ):
                    header = path / name
                    header.parent.mkdir(parents=True, exist_ok=True)
                    header.write_text('#include "api.h"\n')
                # Intercept only the MMIO data pop; execute the real ISR logic.
                source = (ROOT / "drivers/i2c/t5ai.c").read_text()
                source = source.replace("(uint8_t)regs->data", "test_pop()")
                (path / "driver.c").write_text(source)
                prefix = HARNESS.split("int main(void)")[0].replace(
                    '#include "drivers/i2c/t5ai.c"',
                    'static uint8_t test_pop(void);\n#include "driver.c"',
                )
                (path / "test.c").write_text(prefix + FIFO_HARNESS)
                args = [
                    "cc",
                    "-D_GNU_SOURCE",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                ]
                if enabled:
                    args.append("-DCONFIG_T5AI_I2C_RX_FIFO=1")
                if limit4:
                    args.append("-DCONFIG_T5AI_I2C_RX_FIFO_LIMIT4=1")
                subprocess.run(
                    args
                    + ["-I", str(path), str(path / "test.c"), "-o", str(path / "test")],
                    check=True,
                )
                subprocess.run([str(path / "test")], check=True)
