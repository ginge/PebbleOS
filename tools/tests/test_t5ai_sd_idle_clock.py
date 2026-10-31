# SPDX-License-Identifier: Apache-2.0
"""Execute native SD clock lifetime and failure paths against the storage model."""

import unittest

from tools.tests import test_t5ai_sd_pfs as sd_model

PREFIX = r"""
#define CONFIG_T5AI_SD_IDLE_GATING 1
#include "common.h"
#undef NVIC_EnableIRQ
static unsigned irq_enabled, irq_clears, fail_pinctrl, busy_polls, fail_ready;
static int force_dma_status_error;
static void irq_enable(unsigned irq) {assert(irq==SDIO_IRQn);irq_enabled=1;}
static void irq_disable(unsigned irq) {assert(irq==SDIO_IRQn);irq_enabled=0;}
static void irq_clear(unsigned irq) {assert(irq==SDIO_IRQn);++irq_clears;}
#define NVIC_EnableIRQ(irq) irq_enable(irq)
#define NVIC_DisableIRQ(irq) irq_disable(irq)
#define NVIC_ClearPendingIRQ(irq) irq_clear(irq)
"""
MAIN = r"""
static void expect_idle(void) {
 assert(!s_clock_active && !irq_enabled);
 assert(!(SD(SD_GLOBAL_CONTROL)&PERIPHERAL_CLOCK_GATE_BYPASS));
 assert(SD(SD_GLOBAL_CONTROL)&(1u<<31));
 assert(!SD(SD_INTERRUPT_MASK) && !SD(SD_DATA_CONTROL));
 assert(!(SD(SD_FIFO_CONTROL)&(FIFO_RESET|SD_CLOCK_ALWAYS_ON)));
 assert(CH(0,DMA_CHANNEL_CONTROL)==0x123456 && CH(1,DMA_CHANNEL_CONTROL)==0xabcdef);
 assert(REG(0x44010040u)==0xa5a5a5a5u && REG(0x44010038u)==0x12345678u);
 assert(REG(0x45020008u)==0x76543210u);
}
int main(void) {
 map(MODEL_SYSTEM_BASE,MODEL_SYSTEM_SIZE);map(MODEL_GDMA_BASE,MODEL_PERIPHERAL_SIZE);map(MODEL_SDIO_BASE,MODEL_PERIPHERAL_SIZE);
 CH(0,DMA_CHANNEL_CONTROL)=0x123456;CH(1,DMA_CHANNEL_CONTROL)=0xabcdef;
 // No peripheral MMIO before pin initialization has succeeded.
 fail_pinctrl=1;assert(mprotect((void *)MODEL_SDIO_BASE,MODEL_PERIPHERAL_SIZE,PROT_NONE)==0);
 assert(t5ai_sd_init()==-EIO && !s_clock_active && !irq_enabled);
 t5ai_sd_irq();prv_dma_done((void *)1,0);
 assert(mprotect((void *)MODEL_SDIO_BASE,MODEL_PERIPHERAL_SIZE,PROT_READ|PROT_WRITE)==0);
 fail_pinctrl=0;assert(t5ai_sd_init()==0 && !s_faulted);
 REG(0x44010040u)=0xa5a5a5a5u;REG(0x44010038u)=0x12345678u;
 REG(0x45020008u)=0x76543210u;SD(SD_GLOBAL_CONTROL)|=1u<<31;
 expect_idle();
 uint8_t input[4097],output[4097];for(unsigned i=0;i<sizeof(input);++i)input[i]=(uint8_t)(i*13u);
 // Both completion orders and single/multiple sector operations reopen and drain.
 for(unsigned order=0;order<3;++order){
  completion_order=order;busy_polls=3;unsigned before=ready_polls;
  assert(t5ai_sd_write(4,input+1,8)==0);assert(ready_polls>=before+5);expect_idle();
  assert(t5ai_sd_read(4,output+1,8)==0);assert(!memcmp(input+1,output+1,4096));expect_idle();
  assert(t5ai_sd_read(4,output+1,1)==0);expect_idle();
 }
 // Late peripheral/DMA callbacks and rejected/repeated calls must not read gated registers.
 assert(mprotect((void *)MODEL_SDIO_BASE,MODEL_PERIPHERAL_SIZE,PROT_NONE)==0);
 unsigned clears=irq_clears;uint32_t dma_status=s_dma_status;
 t5ai_sd_irq();prv_dma_done((void *)1,0);prv_dma_done(NULL,-EIO);
 assert(t5ai_sd_init()==0 && t5ai_sd_read(0,output,0)==-EINVAL);
 assert(irq_clears==clears && s_dma_status==dma_status && !s_clock_active);
 assert(mprotect((void *)MODEL_SDIO_BASE,MODEL_PERIPHERAL_SIZE,PROT_READ|PROT_WRITE)==0);
 // Uncertain command/DMA completion retains bypass without breaking the transaction.
 prv_clock_acquire();SD(SD_COMMAND_CONTROL)=1;prv_clock_release();assert(s_clock_active && irq_enabled);
 SD(SD_COMMAND_CONTROL)=0;CH(TX_CHANNEL,DMA_CHANNEL_CONTROL)=1;prv_clock_release();assert(s_clock_active);
 CH(TX_CHANNEL,DMA_CHANNEL_CONTROL)=0;force_dma_status_error=1;prv_clock_release();assert(s_clock_active);
 force_dma_status_error=0;prv_clock_release();expect_idle();
 // Shared source/domain/other channel ownership is not released by local idle.
 REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS)|=1u<<31;
 assert(t5ai_sd_read(4,output,1)==0);assert(REG(SYSTEM_SDIO_CLOCK_CONTROL_ADDRESS)&(1u<<31));expect_idle();
 // Card never-ready, DMA bus errors and missing completion quarantine while clocked.
 fail_ready=1;assert(t5ai_sd_read(4,output,1)==-ETIMEDOUT);assert(s_faulted && s_clock_active && irq_enabled);
 assert(!CH(TX_CHANNEL,DMA_CHANNEL_CONTROL) && !CH(RX_CHANNEL,DMA_CHANNEL_CONTROL));
 assert(t5ai_sd_init()==-EIO && t5ai_sd_read(4,output,1)==-ENODEV);
 fail_ready=0;s_faulted=false;fail_data=1;assert(t5ai_sd_read(4,output,1)==-EIO);
 assert(s_faulted && s_clock_active);s_faulted=false;fail_data=2;
 assert(t5ai_sd_read(4,output,1)==-ETIMEDOUT && s_faulted && s_clock_active);
 assert(CH(0,DMA_CHANNEL_CONTROL)==0x123456 && CH(1,DMA_CHANNEL_CONTROL)==0xabcdef);
 puts("PASS: SD local clock lifetime, card busy, shared owners, late IRQ, retry and fault quarantine");
}
"""


def harness():
    source = sd_model.DRIVER[: sd_model.DRIVER.index("int main(void)")]
    source = source.replace(
        "(void)p;(void)state;return 0;}",
        "(void)p;(void)state;return fail_pinctrl?-EIO:0;}",
    )
    source = source.replace(
        "*residue=(CH(c->channel,DMA_CHANNEL_CONTROL)&1)?4:0;return 0;",
        "if(force_dma_status_error)return -EIO;"
        "*residue=(CH(c->channel,DMA_CHANNEL_CONTROL)&1)?4:0;return 0;",
    )
    source = source.replace(
        "(void)unused; ticks+=timeout;",
        "(void)unused; ticks+=timeout;"
        "assert(s_clock_active && irq_enabled && "
        "(SD(SD_GLOBAL_CONTROL)&PERIPHERAL_CLOCK_GATE_BYPASS));",
    )
    source = source.replace(
        "response=0x900; ++ready_polls;",
        "response=(fail_ready || busy_polls)?0xe00:0x900;"
        "if(busy_polls)--busy_polls;++ready_polls;",
    )
    return PREFIX + source + MAIN


class SDIdleClockTest(unittest.TestCase):
    def test_local_gate_four_bit_and_one_bit(self):
        for four_bit in [True, False]:
            with self.subTest(four_bit=four_bit):
                sd_model.SDTests().compile_run(
                    "drivers/t5ai/sd.c", harness(), four_bit=four_bit
                )
