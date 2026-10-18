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
#define PBL_ASSERTN(x) assert(x)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define PBL_GPIO_OUTPUT_HIGH 1u
struct pbl_gpio { unsigned pin; };
enum pbl_i2c_event { PBL_I2C_EVENT_COMPLETE, PBL_I2C_EVENT_ERROR };
enum pbl_i2c_dir { PBL_I2C_READ, PBL_I2C_WRITE };
struct pbl_i2c_transfer { uint16_t addr; enum pbl_i2c_dir dir; bool with_reg;
 uint8_t reg; uint32_t size; uint8_t *data; bool dma; };
struct pbl_i2c_bus_state { struct pbl_i2c_transfer transfer; enum pbl_i2c_event event; };
struct pbl_i2c_bus { struct pbl_i2c_bus_state *state; };
enum t5ai_i2c_phase { T5AI_I2C_REGISTER, T5AI_I2C_RESTART, T5AI_I2C_WRITE, T5AI_I2C_READ };
struct pbl_i2c_t5ai_state { enum t5ai_i2c_phase phase; uint32_t index; bool recovery_required,active; };
struct pbl_i2c_t5ai { struct pbl_i2c_bus bus; struct pbl_i2c_t5ai_state *state;
 uintptr_t base; int irqn; unsigned frequency; struct pbl_gpio scl,sda; };
struct pbl_i2c_bus_ops {
 int (*init)(const struct pbl_i2c_bus *);
 void (*enable)(const struct pbl_i2c_bus *);
 void (*disable)(const struct pbl_i2c_bus *);
 bool (*is_busy)(const struct pbl_i2c_bus *);
 void (*start_transfer)(const struct pbl_i2c_bus *);
 void (*abort_transfer)(const struct pbl_i2c_bus *);
};
int pbl_gpio_configure(const struct pbl_gpio *,uint32_t);
int pbl_gpio_set_raw(const struct pbl_gpio *,bool);
int pbl_gpio_get_raw(const struct pbl_gpio *);
void delay_us(unsigned);
void NVIC_DisableIRQ(int);
void NVIC_ClearPendingIRQ(int);
void NVIC_SetPriority(int,unsigned);
void NVIC_EnableIRQ(int);
void pbl_i2c_bus_event(const struct pbl_i2c_bus *,enum pbl_i2c_event);
"""
HARNESS = r"""
#include "api.h"
#include <sys/mman.h>
#include "drivers/i2c/t5ai.c"
static bool scl_high=true,sda_high=true,irq_enabled,in_isr,stuck_scl;
static unsigned pulses,release_after,elapsed,gpio_calls,completions;
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
void delay_us(unsigned us){elapsed+=us;assert(elapsed<2000);}
void NVIC_DisableIRQ(int irq){assert(irq==6);irq_enabled=false;}
void NVIC_ClearPendingIRQ(int irq){assert(irq==6);}
void NVIC_SetPriority(int irq,unsigned p){assert(irq==6 && p==6);}
void NVIC_EnableIRQ(int irq){assert(irq==6);irq_enabled=true;}
void pbl_i2c_bus_event(const struct pbl_i2c_bus *b,enum pbl_i2c_event e){++completions;b->state->event=e;}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0}; struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,
  .irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 uint8_t data[2]={0x12,0x34};
 state.transfer=(struct pbl_i2c_transfer){.size=2,.data=data,.addr=0x5a,.dir=PBL_I2C_WRITE,.with_reg=false};
 sda_high=false;release_after=3;pbl_i2c_t5ai_ops.init(bus);
 assert(sda_high && pulses==4 && irq_enabled); // Three clocks plus STOP.
 assert(T5AI_I2C_GPIO_BASE[20]&T5AI_I2C_GPIO_SECOND_FUNCTION);
 pbl_i2c_t5ai_ops.enable(bus);pbl_i2c_t5ai_ops.start_transfer(bus);
 unsigned calls=gpio_calls;
 // ISR error must not perform GPIO recovery or wait for clock stretching.
 prv_regs(bus)->status=T5AI_I2C_STATUS_TIMEOUT;in_isr=true;pbl_i2c_t5ai_irq_handler(prv_dev(bus));in_isr=false;
 assert(gpio_calls==calls && !irq_enabled && hw.recovery_required);
 assert(completions==1 && state.event==PBL_I2C_EVENT_ERROR);
 pbl_i2c_t5ai_irq_handler(&dev);assert(completions==1);
 // Next transfer restores power, clock, mux, IRQ and controller settings.
 T5AI_SYSTEM_POWER_SLEEP_WAKEUP|=T5AI_I2C_BACKUP_POWER_DOWN;
 T5AI_SYSTEM_DEVICE_CLOCK_ENABLE=0;T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE=0;
 prv_regs(bus)->config=0;sda_high=false;release_after=0;pulses=elapsed=0;
 pbl_i2c_t5ai_ops.start_transfer(bus);
 assert(pulses==10 && elapsed<1000 && irq_enabled && !hw.recovery_required);
 assert(!(T5AI_SYSTEM_POWER_SLEEP_WAKEUP&T5AI_I2C_BACKUP_POWER_DOWN));
 assert(T5AI_SYSTEM_DEVICE_CLOCK_ENABLE&T5AI_I2C_CLOCK_ENABLE);
 assert(T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE&T5AI_I2C0_INTERRUPT_ENABLE);
 assert(prv_regs(bus)->config&T5AI_I2C_CONFIG_ENABLE);
 assert(prv_regs(bus)->status==T5AI_I2C_STATUS_START);
 // A permanently low SCL is bounded and mux is still restored.
 stuck_scl=true;pulses=elapsed=0;pbl_i2c_t5ai_ops.abort_transfer(bus);pbl_i2c_t5ai_ops.start_transfer(bus);
 assert(pulses==0 && elapsed>=100 && elapsed<200 && irq_enabled);
 assert(T5AI_I2C_GPIO_BASE[21]&T5AI_I2C_GPIO_SECOND_FUNCTION);
 // Successful traffic after recovery delivers exactly the requested bytes.
 stuck_scl=false;prv_regs(bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));assert(prv_regs(bus)->data==0x12);
 prv_regs(bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));assert(prv_regs(bus)->data==0x34);
 prv_regs(bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));assert(completions==2 && state.event==PBL_I2C_EVENT_COMPLETE);
 return 0;
}
"""


class T5AII2CRecoveryTest(unittest.TestCase):
    def test_stuck_bus_and_deferred_recovery(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "api.h").write_text(API)
            headers = [
                "pbl/util/misc.h",

                "pbl/drivers/i2c/t5ai.h",

                "pbl/kernel/kernel.h",
                "pbl/soc/t5ai/delay.h",
                "semphr.h",
                "system/passert.h",
                "cmsis_core.h",
            ]
            for name in headers:
                header = path / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "api.h"\n')
            (path / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-D_GNU_SOURCE",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-I",
                    str(path),
                    "-I",
                    str(ROOT),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
