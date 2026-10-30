# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise recovery of a stuck slave and deferred controller reset."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

API = r"""
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pbl/drivers/i2c/t5ai.h>
void NVIC_DisableIRQ(int);
void NVIC_ClearPendingIRQ(int);
void NVIC_SetPriority(int,unsigned);
void NVIC_EnableIRQ(int);
"""


def write_headers(path):
    headers = {
        "pbl/util/misc.h": "#include <stddef.h>\n#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))\n",
        "pbl/kernel/mutex.h": "#pragma once\nstruct pbl_mutex {int unused;};\n",
        "pbl/kernel/sem.h": "#pragma once\nstruct pbl_sem {int unused;};\n",
        "pbl/kernel/irq.h": "void pbl_irq_lock(void);\nvoid pbl_irq_unlock(void);\n",
        "pbl/kernel/assert.h": "#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n",
        "pbl/soc/t5ai/delay.h": "void delay_us(unsigned);\n",
        "cmsis_core.h": '#include "api.h"\n',
    }
    (path / "api.h").write_text(API)
    for name, content in headers.items():
        header = path / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text(content)


HARNESS = r"""
#include "api.h"
#include <sys/mman.h>
#include "drivers/i2c/t5ai.c"
static bool scl_high=true,sda_high=true,irq_enabled,in_isr,stuck_scl;
static unsigned pulses,release_after,elapsed,gpio_calls,completions,pin_calls,stop_release_at;
int pbl_pinctrl_select_state(const struct pbl_pinctrl *pins,enum pbl_pinctrl_state_id id){assert(!in_isr && id==PBL_PINCTRL_STATE_DEFAULT);++pin_calls;return 0;}
int pbl_gpio_configure(const struct pbl_gpio *p,uint32_t mode) {
 assert(!in_isr && mode==PBL_GPIO_OUTPUT_HIGH);++gpio_calls;
 if(p->pin==20)scl_high=true;
 return 0;
}
int pbl_gpio_set_raw(const struct pbl_gpio *p,bool high) {
 assert(!in_isr);++gpio_calls;
 if(p->pin==20) {
  if(high && !scl_high) {++pulses;if(release_after && pulses>=release_after)sda_high=true;}
  scl_high=high;
 }
 return 0;
}
int pbl_gpio_get_raw(const struct pbl_gpio *p){return p->pin==20 ? scl_high && !stuck_scl : sda_high;}
void delay_us(unsigned us){
 elapsed+=us;assert(elapsed<2000);
 if(stop_release_at){
  T5AII2CRegisters *regs=(T5AII2CRegisters *)T5AI_I2C0_BASE;
  assert(regs->config&T5AI_I2C_CONFIG_ENABLE);
  if(elapsed>=stop_release_at){regs->status&=~T5AI_I2C_STATUS_BUSY;stop_release_at=0;}
 }
}
void NVIC_DisableIRQ(int irq){assert(irq==6);irq_enabled=false;}
void NVIC_ClearPendingIRQ(int irq){assert(irq==6);}
void NVIC_SetPriority(int irq,unsigned p){assert(irq==6 && p==6);}
void NVIC_EnableIRQ(int irq){assert(irq==6);irq_enabled=true;}
static unsigned irq_depth;
void pbl_irq_lock(void){++irq_depth;}
void pbl_irq_unlock(void){assert(irq_depth);--irq_depth;}
void pbl_i2c_bus_complete(const struct pbl_i2c_bus *b,int status){++completions;b->state->status=status;}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0}; struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,
  .irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 uint8_t data[2]={0x12,0x34};
 struct pbl_i2c_msg msg={.len=2,.buf=data,.flags=PBL_I2C_MSG_WRITE};
 sda_high=false;release_after=3;pbl_i2c_t5ai_ops.init(bus);
 assert(sda_high && pulses==4 && irq_enabled); // Three clocks plus STOP.
 assert(pin_calls==1);
 pbl_i2c_t5ai_ops.enable(bus);assert(pbl_i2c_t5ai_ops.start(bus,0x5a,&msg,1)==0);
 unsigned calls=gpio_calls;
 // ISR error must not perform GPIO recovery or wait for clock stretching.
 prv_regs(bus)->status=T5AI_I2C_STATUS_TIMEOUT;in_isr=true;pbl_i2c_t5ai_irq_handler(prv_dev(bus));in_isr=false;
 assert(gpio_calls==calls && !irq_enabled && hw.recovery_required);
 assert(completions==1 && state.status==-ETIMEDOUT);
 pbl_i2c_t5ai_irq_handler(&dev);assert(completions==1);
 // Next transfer restores power, clock, mux, IRQ and controller settings.
 T5AI_SYSTEM_POWER_SLEEP_WAKEUP|=T5AI_I2C_BACKUP_POWER_DOWN;
 T5AI_SYSTEM_DEVICE_CLOCK_ENABLE=0;T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE=0;
 prv_regs(bus)->config=0;sda_high=false;release_after=0;pulses=elapsed=0;
 assert(pbl_i2c_t5ai_ops.start(bus,0x5a,&msg,1)==0);
 assert(pulses==10 && elapsed<1000 && irq_enabled && !hw.recovery_required);
 assert(!(T5AI_SYSTEM_POWER_SLEEP_WAKEUP&T5AI_I2C_BACKUP_POWER_DOWN));
 assert(T5AI_SYSTEM_DEVICE_CLOCK_ENABLE&T5AI_I2C_CLOCK_ENABLE);
 assert(T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE&T5AI_I2C0_INTERRUPT_ENABLE);
 assert(prv_regs(bus)->config&T5AI_I2C_CONFIG_ENABLE);
 assert(prv_regs(bus)->status==T5AI_I2C_STATUS_START);
 // A permanently low SCL is bounded and mux is still restored.
 stuck_scl=true;pulses=elapsed=0;pbl_i2c_t5ai_ops.abort(bus);assert(pbl_i2c_t5ai_ops.start(bus,0x5a,&msg,1)==0);
 assert(pulses==0 && elapsed>=100 && elapsed<200 && irq_enabled);
 assert(pin_calls==3);
 // Successful traffic after recovery delivers exactly the requested bytes.
 stuck_scl=false;prv_regs(bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));assert(prv_regs(bus)->data==0x12);
 prv_regs(bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));assert(prv_regs(bus)->data==0x34);
 prv_regs(bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));assert(completions==2 && state.status==0);
 return 0;
}
"""


MESSAGE_HARNESS = r"""
static void irq(const struct pbl_i2c_bus *bus, uint32_t status) {
 prv_regs(bus)->status=status;in_isr=true;pbl_i2c_t5ai_irq_handler(prv_dev(bus));in_isr=false;
}
static void ack(const struct pbl_i2c_bus *bus) {
 irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK);
}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0};struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,
  .irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 pbl_i2c_t5ai_ops.init(bus);pbl_i2c_t5ai_ops.enable(bus);
 uint8_t address=0x30,data[]={0x12,0x34},read[2]={0};
 struct pbl_i2c_msg msgs[]={
  {.buf=&address,.len=1,.flags=PBL_I2C_MSG_WRITE},
  {.len=0,.flags=PBL_I2C_MSG_WRITE|PBL_I2C_MSG_NOSTART},
  {.buf=data,.len=2,.flags=PBL_I2C_MSG_WRITE|PBL_I2C_MSG_NOSTART},
  {.buf=read,.len=2,.flags=PBL_I2C_MSG_READ},
 };
 assert(pbl_i2c_t5ai_ops.start(bus,0x6b,msgs,4)==0);
 assert(prv_regs(bus)->data==0xd6);ack(bus);assert(prv_regs(bus)->data==address);
 ack(bus);assert(prv_regs(bus)->data==data[0] && prv_regs(bus)->status==0);
 ack(bus);assert(prv_regs(bus)->data==data[1] && !completions);
 ack(bus);assert(prv_regs(bus)->data==0xd7 && prv_regs(bus)->status==T5AI_I2C_STATUS_START);
 ack(bus);assert(!completions);
 prv_regs(bus)->data=0x56;ack(bus);assert(read[0]==0x56 && !completions);
 prv_regs(bus)->data=0x78;ack(bus);
 assert(read[1]==0x78 && completions==1 && state.status==0 && !hw.active);
 assert(prv_regs(bus)->status==T5AI_I2C_STATUS_STOP);
 // Separate write messages generate repeated START, while NOSTART joins data.
 msgs[1]=(struct pbl_i2c_msg){.buf=data,.len=1,.flags=PBL_I2C_MSG_WRITE};
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,2)==0);
 ack(bus);ack(bus);
 assert(prv_regs(bus)->data==0xa2 && prv_regs(bus)->status==T5AI_I2C_STATUS_START);
 ack(bus);assert(prv_regs(bus)->data==0x12);ack(bus);
 assert(completions==2 && state.status==0);
 // Address and data NACKs have distinct errno values and complete once.
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,1)==0);
 irq(bus,T5AI_I2C_STATUS_INTERRUPT);
 assert(completions==3 && state.status==-ENXIO && !hw.active);
 ack(bus);assert(completions==3);
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,1)==0);ack(bus);
 irq(bus,T5AI_I2C_STATUS_INTERRUPT);
 assert(completions==4 && state.status==-EIO);
 // Class timeout abort must suppress even a late successful interrupt.
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,1)==0);
 pbl_i2c_t5ai_ops.abort(bus);ack(bus);assert(completions==4 && !hw.active);
 // Unsupported requests are rejected before touching the controller.
 elapsed=0;uint32_t before=prv_regs(bus)->status;
 msgs[0].flags=PBL_I2C_MSG_READ;
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,2)==-ENOTSUP);
 assert(pbl_i2c_t5ai_ops.start(bus,0x80,msgs,1)==-EINVAL);
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,0)==-EINVAL);
 msgs[0].flags=0x8000;
 assert(pbl_i2c_t5ai_ops.start(bus,0x51,msgs,1)==-ENOTSUP);
 assert(prv_regs(bus)->status==before && !elapsed && !irq_depth);
 return 0;
}
"""


class T5AII2CRecoveryTest(unittest.TestCase):
    def test_stuck_bus_and_deferred_recovery(self):
        self.run_harness(HARNESS)

    def test_message_sequences_errors_and_abort(self):
        self.run_harness(HARNESS.split("int main(void)")[0] + MESSAGE_HARNESS)

    def test_stop_drains_before_disable_and_stuck_stop_recovers(self):
        main = r"""
int main(void){
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0};struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,.irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 pbl_i2c_t5ai_ops.init(bus);pbl_i2c_t5ai_ops.enable(bus);
 elapsed=0;stop_release_at=15;prv_regs(bus)->status=T5AI_I2C_STATUS_BUSY;
 pbl_i2c_t5ai_ops.disable(bus);
 assert(elapsed==20 && !hw.recovery_required && !(prv_regs(bus)->config&T5AI_I2C_CONFIG_ENABLE));
 pbl_i2c_t5ai_ops.enable(bus);elapsed=0;prv_regs(bus)->status=T5AI_I2C_STATUS_BUSY;
 pbl_i2c_t5ai_ops.disable(bus);assert(elapsed==205 && hw.recovery_required);
 uint8_t value=0;struct pbl_i2c_msg msg={.len=1,.buf=&value};elapsed=0;
 assert(pbl_i2c_t5ai_ops.start(bus,0x6b,&msg,1)==0 && !hw.recovery_required && irq_enabled);
 return 0;
}
"""
        self.run_harness(HARNESS.split("int main(void)")[0] + main)

    def run_harness(self, harness):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            write_headers(path)
            (path / "test.c").write_text(harness)
            subprocess.run(
                [
                    "clang-21",
                    "-D_GNU_SOURCE",
                    "-std=gnu23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-pie",
                    "-no-pie",
                    "-I",
                    str(path),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
