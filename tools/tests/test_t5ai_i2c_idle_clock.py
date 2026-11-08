# SPDX-License-Identifier: Apache-2.0
"""Execute the opt-in local clock lifetime against STOP, faults and reuse."""

import unittest

from tools.tests import test_t5ai_i2c as base

MAIN = r"""
static void irq(const struct pbl_i2c_bus *bus, uint32_t status) {
 prv_regs(bus)->status=status;in_isr=true;
 pbl_i2c_t5ai_irq_handler(prv_dev(bus));in_isr=false;
}
static void idle(const struct pbl_i2c_bus *bus) {
 assert(!irq_enabled && !prv_dev(bus)->state->active);
 assert(!(prv_regs(bus)->global_control&T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS));
 assert(prv_regs(bus)->global_control&T5AI_I2C_GLOBAL_RESET_RELEASE);
 assert(!(prv_regs(bus)->config&T5AI_I2C_CONFIG_ENABLE));
 // Local gating must leave all source/domain/route bits alone.
 assert(T5AI_SYSTEM_DEVICE_CLOCK_ENABLE==0xa5000001u);
 assert(T5AI_SYSTEM_POWER_SLEEP_WAKEUP==0x5a000000u);
 assert(T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE==0x80000040u);
}
int main(void) {
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0};struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,
  .irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 T5AI_SYSTEM_DEVICE_CLOCK_ENABLE=0xa5000000u;
 T5AI_SYSTEM_POWER_SLEEP_WAKEUP=0x5a000010u;
 T5AI_SYSTEM_CPU1_INTERRUPT_ENABLE=0x80000000u;
 uint8_t value=0x42;struct pbl_i2c_msg msg={.buf=&value,.len=1};
 for(unsigned i=0;i<3;++i) {
  elapsed=0;assert(!pbl_i2c_t5ai_ops.init(bus));idle(bus);
  pbl_i2c_t5ai_ops.enable(bus);assert(irq_enabled);
  assert(prv_regs(bus)->global_control&T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS);
  assert(!pbl_i2c_t5ai_ops.start(bus,0x51,&msg,1));
  irq(bus,T5AI_I2C_STATUS_ACK|T5AI_I2C_STATUS_INTERRUPT);
  irq(bus,T5AI_I2C_STATUS_ACK|T5AI_I2C_STATUS_INTERRUPT);
  assert(completions==i+1 && !state.status && !hw.active);
  assert(prv_regs(bus)->global_control&T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS);
  // Completion precedes physical STOP: bypass must remain until it drains.
  elapsed=0;stop_release_at=15;prv_regs(bus)->status=T5AI_I2C_STATUS_BUSY;
  pbl_i2c_t5ai_ops.disable(bus);assert(elapsed==20);idle(bus);
  pbl_i2c_t5ai_ops.disable(bus);idle(bus); // Idempotent release.
  irq(bus,T5AI_I2C_STATUS_INTERRUPT);assert(completions==i+1);
 }
 // A rejected kickoff still balances the enable in the common transfer wrapper.
 pbl_i2c_t5ai_ops.enable(bus);
 assert(pbl_i2c_t5ai_ops.start(bus,0x80,&msg,1)==-EINVAL);
 prv_regs(bus)->status=0;pbl_i2c_t5ai_ops.disable(bus);idle(bus);
 // NACK requests STOP and releases only from task context.
 pbl_i2c_t5ai_ops.enable(bus);assert(!pbl_i2c_t5ai_ops.start(bus,0x51,&msg,1));
 irq(bus,T5AI_I2C_STATUS_INTERRUPT);assert(state.status==-ENXIO && completions==4);
 pbl_i2c_t5ai_ops.disable(bus);idle(bus);
 // Stuck STOP fails open: no clock loss while the bus may still be active.
 pbl_i2c_t5ai_ops.enable(bus);elapsed=0;
 prv_regs(bus)->status=T5AI_I2C_STATUS_BUSY;pbl_i2c_t5ai_ops.disable(bus);
 assert(elapsed==205 && hw.recovery_required);
 assert(prv_regs(bus)->global_control&T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS);
 // The next transaction recovers and reacquires bypass/IRQ before START.
 elapsed=0;pbl_i2c_t5ai_ops.enable(bus);
 assert(!pbl_i2c_t5ai_ops.start(bus,0x51,&msg,1));
 assert(!hw.recovery_required && hw.active && irq_enabled);
 assert(prv_regs(bus)->global_control&T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS);
 unsigned calls=gpio_calls;
 irq(bus,T5AI_I2C_STATUS_TIMEOUT);
 assert(completions==5 && state.status==-ETIMEDOUT && gpio_calls==calls);
 assert(hw.recovery_required && !irq_enabled);
 pbl_i2c_t5ai_ops.disable(bus);idle(bus);
 // Cancellation and a late IRQ do not complete twice; recovery stays deferred.
 elapsed=0;pbl_i2c_t5ai_ops.enable(bus);
 assert(!pbl_i2c_t5ai_ops.start(bus,0x51,&msg,1));
 pbl_i2c_t5ai_ops.abort(bus);pbl_i2c_t5ai_ops.abort(bus);
 pbl_i2c_t5ai_ops.disable(bus);idle(bus);
 irq(bus,T5AI_I2C_STATUS_INTERRUPT|T5AI_I2C_STATUS_ACK);assert(completions==5);
 return 0;
}
"""


class T5AII2CIdleClockTest(unittest.TestCase):
    def test_stop_fault_cancel_and_repeated_acquisition(self):
        prefix = base.HARNESS.split("int main(void)")[0]
        prefix = prefix.replace(
            "assert(regs->config&T5AI_I2C_CONFIG_ENABLE);",
            "assert(regs->config&T5AI_I2C_CONFIG_ENABLE);"
            "assert(regs->global_control&T5AI_I2C_GLOBAL_CLOCK_GATE_BYPASS);",
        )
        base.T5AII2CRecoveryTest.run_harness(
            self, "#define CONFIG_T5AI_I2C_IDLE_GATING 1\n" + prefix + MAIN
        )

    def test_retained_owner_survives_stuck_stop_and_recovery(self):
        prefix = base.HARNESS.split("int main(void)")[0]
        spies = r"""
#include <pbl/soc/t5ai/sleep.h>
static unsigned held;
void t5ai_sleep_block(T5AISleepOwner owner){assert(owner==T5AISleepOwner_I2C && !held);++held;}
void t5ai_sleep_unblock(T5AISleepOwner owner){assert(owner==T5AISleepOwner_I2C && held==1);--held;}
void t5ai_sleep_fault(void){assert(0);}
"""
        prefix = prefix.replace(
            '#include "drivers/i2c/t5ai.c"', spies + '\n#include "drivers/i2c/t5ai.c"'
        )
        main = MAIN.replace(
            "assert(elapsed==205 && hw.recovery_required);",
            "assert(held==1);assert(elapsed==205 && hw.recovery_required);",
        )
        main = main.replace("return 0;", "assert(!held);return 0;")
        base.T5AII2CRecoveryTest.run_harness(
            self,
            "#define CONFIG_T5AI_I2C_IDLE_GATING 1\n#define CONFIG_T5AI_RETAINED_SLEEP 1\n"
            + prefix
            + main,
        )
