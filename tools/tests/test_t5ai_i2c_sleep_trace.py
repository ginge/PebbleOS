# SPDX-License-Identifier: Apache-2.0
"""Keep first failed I2C state before cleanup; log only from task context."""

import unittest
from unittest.mock import patch

from tools.tests import test_t5ai_i2c as base

MAIN = r"""
static unsigned logs;
void t5ai_test_log(const char *format,...){(void)format;assert(!in_isr && !irq_depth);++logs;}
int main(void){
 assert(mmap((void *)0x44000000,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 assert(mmap((void *)T5AI_I2C0_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=(void *)-1);
 struct pbl_i2c_bus_state state={0};struct pbl_i2c_t5ai_state hw={0};
 struct pbl_i2c_t5ai dev={.bus={.state=&state},.state=&hw,.base=T5AI_I2C0_BASE,.irqn=6,.frequency=100000,.scl={.pin=20},.sda={.pin=21}};
 const struct pbl_i2c_bus *bus=&dev.bus;
 pbl_i2c_t5ai_ops.init(bus);pbl_i2c_t5ai_ops.enable(bus);
 uint8_t value=0;struct pbl_i2c_msg msg={.len=1,.buf=&value};
 assert(!pbl_i2c_t5ai_ops.start(bus,0x6b,&msg,1));
 fake_nvic.ISER[0]=0x40;fake_nvic.ISPR[0]=0x40;fake_scb.ICSR=0x806;
 T5AI_SYSTEM_CPU1_INTERRUPT_STATUS=0x40;T5AI_SYSTEM_CP_CONTROL=0x12345611;
 const uint32_t original_status=CASE?T5AI_I2C_STATUS_TIMEOUT:T5AI_I2C_STATUS_INTERRUPT;
 prv_regs(bus)->status=original_status;in_isr=true;pbl_i2c_t5ai_irq_handler(&dev);in_isr=false;
 assert(s_first_failure.captured && !s_first_failure.reported && !logs);
 assert(s_first_failure.result==(CASE?-ETIMEDOUT:-ENXIO));
 assert(s_first_failure.status==original_status && s_first_failure.address==0x6b);
 assert(s_first_failure.config&T5AI_I2C_CONFIG_ENABLE);
 assert(s_first_failure.nvic_enable==0x40 && s_first_failure.nvic_pending==0x40);
 assert(s_first_failure.pending==0x40 && s_first_failure.cp==0x12345611);
 assert(prv_regs(bus)->status!=original_status); // STOP/abort changed hardware after capture.
 pbl_i2c_t5ai_ops.disable(bus);assert(s_first_failure.reported && logs==4);
 prv_regs(bus)->status=0;prv_abort(bus);pbl_i2c_t5ai_ops.disable(bus);
 assert(s_first_failure.status==original_status && logs==4);
 return 0;
}
"""


class I2CSleepTrace(unittest.TestCase):
    def test_first_failure_survives_abort_and_defers_isr_logging(self):
        original = base.write_headers

        def headers(path):
            original(path)
            with (path / "api.h").open("a") as f:
                f.write("""
#define CONFIG_T5AI_RETAINED_SLEEP_TEST 1
static struct {uint32_t ISER[2],ISPR[2];} fake_nvic;
static struct {uint32_t ICSR;} fake_scb;
#define NVIC (&fake_nvic)
#define SCB (&fake_scb)
#define __get_BASEPRI() 0u
void t5ai_test_log(const char *,...);
""")
            q = path / "pbl/logging/logging.h"
            q.parent.mkdir(parents=True, exist_ok=True)
            q.write_text(
                "#define PBL_LOG_MODULE_DEFINE(...)\n#define PBL_LOG_ERR(...) t5ai_test_log(__VA_ARGS__)\n"
            )

        for case in range(2):
            with self.subTest(case=case), patch.object(base, "write_headers", headers):
                base.T5AII2CRecoveryTest().run_harness(
                    f"#define CASE {case}\n"
                    + base.HARNESS.split("int main(void)")[0]
                    + MAIN
                )
