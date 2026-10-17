# SPDX-License-Identifier: Apache-2.0
"""Exercise recovery of a stuck slave and deferred controller reset."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

API = r"""
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int portBASE_TYPE;
struct pbl_sem { unsigned count; };
typedef struct { uint8_t pin; bool active_high; } OutputConfig;
typedef struct { uint8_t pin; } InputConfig;
#define GPIO_OType_OD 1
#define PBL_ASSERTN(x) assert(x)
#define portEND_SWITCHING_ISR(x) ((void)(x))
typedef enum {I2CTransferEvent_TransferComplete,I2CTransferEvent_Error,I2CTransferEvent_NackReceived} I2CTransferEvent;
typedef enum {I2CTransferDirection_Read,I2CTransferDirection_Write} I2CTransferDirection;
typedef enum {I2CTransferType_NoRegisterAddress,I2CTransferType_SendRegisterAddress} I2CTransferType;
typedef enum {I2CTransferState_ReadData,I2CTransferState_WriteData,I2CTransferState_WriteRegAddress,I2CTransferState_RepeatStart} I2CTransferState;
typedef struct { unsigned idx,size; uint16_t device_address; uint8_t register_address; uint8_t *data; I2CTransferDirection direction; I2CTransferType type; I2CTransferState state; } I2CTransfer;
typedef struct { I2CTransfer transfer; I2CTransferEvent transfer_event; struct pbl_sem event_semaphore; } I2CBusState;
typedef struct { uintptr_t base; int irqn; unsigned frequency; uint8_t scl_pin,sda_pin; } I2CBusHal;
typedef struct { I2CBusState *state; I2CBusHal *hal; } I2CBus;
void gpio_output_init(const OutputConfig *,int);
void gpio_output_set(const OutputConfig *,bool);
bool gpio_input_read(const InputConfig *);
void delay_us(unsigned);
void NVIC_DisableIRQ(int);
void NVIC_ClearPendingIRQ(int);
void NVIC_SetPriority(int,unsigned);
void NVIC_EnableIRQ(int);
void pbl_sem_give(struct pbl_sem *);
int i2c_handle_transfer_event(I2CBus *,I2CTransferEvent);
void i2c_hal_abort_transfer(I2CBus *);
"""
HARNESS = r"""
#include "api.h"
#include <sys/mman.h>
#include "drivers/i2c/t5ai.c"
static bool scl_high=true,sda_high=true,irq_enabled,in_isr,stuck_scl;
static unsigned pulses,release_after,elapsed,gpio_calls,completions;
void gpio_output_init(const OutputConfig *p,int mode) {
 assert(!in_isr && mode==GPIO_OType_OD && !p->active_high);++gpio_calls;
 if(p->pin==20)scl_high=true;
}
void gpio_output_set(const OutputConfig *p,bool asserted) {
 assert(!in_isr);++gpio_calls;
 if(p->pin==20) {
  if(!asserted && !scl_high) {++pulses;if(release_after && pulses>=release_after)sda_high=true;}
  scl_high=!asserted;
 }
}
bool gpio_input_read(const InputConfig *p){return p->pin==20 ? scl_high && !stuck_scl : sda_high;}
void delay_us(unsigned us){elapsed+=us;assert(elapsed<2000);}
void NVIC_DisableIRQ(int irq){assert(irq==6);irq_enabled=false;}
void NVIC_ClearPendingIRQ(int irq){assert(irq==6);}
void NVIC_SetPriority(int irq,unsigned p){assert(irq==6 && p==6);}
void NVIC_EnableIRQ(int irq){assert(irq==6);irq_enabled=true;}
void pbl_sem_give(struct pbl_sem *s){(void)s;}
int i2c_handle_transfer_event(I2CBus *b,I2CTransferEvent e){++completions;b->state->transfer_event=e;return 0;}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 I2CBusHal hal={.base=T5AI_I2C0_BASE,.irqn=6,.frequency=100000,.scl_pin=20,.sda_pin=21};
 I2CBusState state={0};I2CBus bus={.hal=&hal,.state=&state};
 uint8_t data[2]={0x12,0x34};
 state.transfer=(I2CTransfer){.size=2,.data=data,.device_address=0x5a,.direction=I2CTransferDirection_Write,.type=I2CTransferType_NoRegisterAddress};
 sda_high=false;release_after=3;i2c_hal_init(&bus);
 assert(sda_high && pulses==4 && irq_enabled); // Three clocks plus STOP.
 assert(T5AI_I2C_GPIO_BASE[20]&T5AI_I2C_GPIO_SECOND_FUNCTION);
 i2c_hal_enable(&bus);i2c_hal_start_transfer(&bus);
 unsigned calls=gpio_calls;
 // ISR error must not perform GPIO recovery or wait for clock stretching.
 prv_regs(&bus)->status=T5AI_I2C_STATUS_TIMEOUT;in_isr=true;i2c_irq_handler(&bus);in_isr=false;
 assert(gpio_calls==calls && !irq_enabled && s_recovery_required);
 assert(completions==1 && state.transfer_event==I2CTransferEvent_Error);
 // Next transfer restores power, clock, mux, IRQ and controller settings.
 T5AI_SYSTEM_POWER_SLEEP_WAKEUP|=T5AI_I2C_BACKUP_POWER_DOWN;
 T5AI_SYSTEM_DEVICE_CLOCK_ENABLE=0;T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE=0;
 prv_regs(&bus)->config=0;sda_high=false;release_after=0;pulses=elapsed=0;
 i2c_hal_start_transfer(&bus);
 assert(pulses==10 && elapsed<1000 && irq_enabled && !s_recovery_required);
 assert(!(T5AI_SYSTEM_POWER_SLEEP_WAKEUP&T5AI_I2C_BACKUP_POWER_DOWN));
 assert(T5AI_SYSTEM_DEVICE_CLOCK_ENABLE&T5AI_I2C_CLOCK_ENABLE);
 assert(T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE&T5AI_I2C0_INTERRUPT_ENABLE);
 assert(prv_regs(&bus)->config&T5AI_I2C_CONFIG_ENABLE);
 assert(prv_regs(&bus)->status==T5AI_I2C_STATUS_START);
 // A permanently low SCL is bounded and mux is still restored.
 stuck_scl=true;pulses=elapsed=0;i2c_hal_abort_transfer(&bus);i2c_hal_start_transfer(&bus);
 assert(pulses==0 && elapsed>=100 && elapsed<200 && irq_enabled);
 assert(T5AI_I2C_GPIO_BASE[21]&T5AI_I2C_GPIO_SECOND_FUNCTION);
 // Successful traffic after recovery delivers exactly the requested bytes.
 stuck_scl=false;prv_regs(&bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 i2c_irq_handler(&bus);assert(prv_regs(&bus)->data==0x12);
 prv_regs(&bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 i2c_irq_handler(&bus);assert(prv_regs(&bus)->data==0x34);
 prv_regs(&bus)->status=T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK;
 i2c_irq_handler(&bus);assert(completions==2 && state.transfer_event==I2CTransferEvent_TransferComplete);
 return 0;
}
"""


class T5AII2CRecoveryTest(unittest.TestCase):
    def test_stuck_bus_and_deferred_recovery(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "api.h").write_text(API)
            headers = [
                "pbl/drivers/i2c/t5ai_legacy_hal.h",
                "pbl/drivers/i2c/t5ai_legacy_definitions.h",
                "pbl/drivers/i2c/t5ai.h",
                "pbl/drivers/gpio/t5ai_legacy.h",
                "pbl/soc/t5ai/kernel.h",
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
