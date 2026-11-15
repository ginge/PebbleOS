# SPDX-License-Identifier: Apache-2.0
"""Execute native DMA with modeled W1C registers and isolated AP channels."""

import re
import unittest

from tools.tests.native_t5ai_harness import ROOT, run

PREFIX = (
    f'#include "{ROOT / "include/pbl/soc/t5ai/memory.h"}"\n'
    + r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define nullptr NULL
#define __DSB() ((void)0)
#define GDMA_IRQn 11
#define T5AI_DMA_PERIPHERAL_INCREMENT 1u
#define T5AI_DMA_BURST_INC16 2u
static uint32_t registers[1024],system_regs[128];
static unsigned depth,callbacks,last_channel;static int last_status;
struct pbl_device {int unused;};
enum pbl_dma_dir {PBL_DMA_MEM_TO_DEV,PBL_DMA_DEV_TO_MEM};
struct pbl_dma_slave_config {uintptr_t src_addr,dst_addr;unsigned src_addr_width,dst_addr_width;};
struct pbl_dma_controller;
struct pbl_dma_chan {const struct pbl_dma_controller *ctrl;uint8_t channel,request;uint32_t config;};
struct pbl_dma_chan_state {struct pbl_dma_slave_config cfg;};
struct pbl_dma_controller {struct pbl_device dev;struct pbl_dma_chan_state *chans;};
static struct pbl_dma_chan_state channels[4];
static const struct pbl_dma_controller T5AI_DMA={.chans=channels};
struct pbl_dma_controller_ops {
 int (*slave_config)(const struct pbl_dma_controller *,const struct pbl_dma_chan *,const struct pbl_dma_slave_config *);
 int (*prep_slave_single)(const struct pbl_dma_controller *,uint8_t,void *,size_t,enum pbl_dma_dir);
 int (*prep_dma_cyclic)(const struct pbl_dma_controller *,uint8_t,void *,size_t,size_t,enum pbl_dma_dir);
 int (*terminate)(const struct pbl_dma_controller *,uint8_t);
 int (*tx_status)(const struct pbl_dma_controller *,uint8_t,size_t *);
};
static void pbl_irq_lock(void){++depth;}
static void pbl_irq_unlock(void){assert(depth);--depth;}
static void delay_us(unsigned us){assert(us==10);}
static void NVIC_SetPriority(unsigned irq,unsigned priority){assert(irq==11 && priority==6);}
static void NVIC_EnableIRQ(unsigned irq){assert(irq==11);}
static void pbl_dma_complete(const struct pbl_dma_controller *ctrl,uint8_t channel,int status){assert(depth);++callbacks;last_channel=channel;last_status=status;}
"""
)
MAIN = r"""
#define CH(c, offset) DMA_CHANNEL_REG(c, offset)
#define FINISH DMA_STATUS_FINISH
#define HALF DMA_STATUS_HALF
#define ERROR DMA_STATUS_BUS_ERROR
int main(void){
 registers[(0x40+12*0x40)/4]=0xabcdef;
 assert(prv_init(NULL)==0 && !depth && registers[(0x40+12*0x40)/4]==0xabcdef);
 for(unsigned c=0;c<4;++c){
  struct pbl_dma_chan chan={&T5AI_DMA,c,c?14:0,c?0:3};
  const struct pbl_dma_slave_config cfg={.src_addr=0x47000044,.dst_addr=0x64000000,.src_addr_width=4,.dst_addr_width=4};
  assert(prv_config(&T5AI_DMA,&chan,&cfg)==0);channels[c].cfg=cfg;
  assert(prv_single(&T5AI_DMA,c,(void *)0x28020000,0,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x28020001,4,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x28020000,65540,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x60022000,4,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x60027ffc,8,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x60028000,4,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x60700000,4,PBL_DMA_MEM_TO_DEV)==-EINVAL);
  assert(prv_single(&T5AI_DMA,c,(void *)0x60024000,14912,PBL_DMA_MEM_TO_DEV)==0);
  assert(CH(c,8)==0x60024000);prv_terminate(&T5AI_DMA,c);
  assert(prv_single(&T5AI_DMA,c,(void *)0x28020000,932,PBL_DMA_MEM_TO_DEV)==0);
  assert(CH(c,4)==0x64000000 && CH(c,8)==0x28020000 && (CH(c,0)>>16)==931);
  assert(CH(c,0x0c)==0x64000000u+(c==0?936u:4u));
  assert(CH(c,0x14)==0x28020000u+(c==0?936u:932u));
  assert((CH(c,0)&(1u<<8)) && !!(CH(c,0)&(1u<<9))==(c==0));
  assert(prv_config(&T5AI_DMA,&chan,&cfg)==-EBUSY);
  assert(prv_single(&T5AI_DMA,c,(void *)0x28021000,4,PBL_DMA_MEM_TO_DEV)==-EBUSY);
  size_t residue=999;CH(c,0x28)=0x28020000+400;
  assert(prv_status(&T5AI_DMA,c,&residue)==0 && residue==532);
  unsigned previous=callbacks;CH(c,0x30)=FINISH;pbl_dma_t5ai_irq_handler();
  assert(callbacks==previous+1 && last_channel==c && !last_status && !CH(c,0));
  assert(prv_status(&T5AI_DMA,c,&residue)==0 && !residue);
  assert(prv_single(&T5AI_DMA,c,(void *)0x28020000,932,PBL_DMA_DEV_TO_MEM)==0);
  CH(c,0x30)=ERROR;pbl_dma_t5ai_irq_handler();assert(last_status==-EIO && !CH(c,0));
  assert(prv_cyclic(&T5AI_DMA,c,(void *)0x28020000,2560,320,PBL_DMA_DEV_TO_MEM)==-EINVAL);
  assert(prv_cyclic(&T5AI_DMA,c,(void *)0x28020000,2560,1280,PBL_DMA_DEV_TO_MEM)==0);
  previous=callbacks;CH(c,0x30)=HALF;pbl_dma_t5ai_irq_handler();assert(callbacks==previous+1 && !last_status && CH(c,0));
  CH(c,0x30)=FINISH;pbl_dma_t5ai_irq_handler();assert(callbacks==previous+2 && !last_status && CH(c,0));
  CH(c,0x30)=HALF|FINISH;pbl_dma_t5ai_irq_handler();assert(last_status==-EIO && !CH(c,0));
  assert(prv_single(&T5AI_DMA,c,(void *)0x28020000,932,PBL_DMA_MEM_TO_DEV)==0);
  previous=callbacks;prv_terminate(&T5AI_DMA,c);CH(c,0x30)=FINISH;pbl_dma_t5ai_irq_handler();assert(callbacks==previous);
 }
 assert(!depth && registers[(0x40+12*0x40)/4]==0xabcdef);return 0;
}
"""


def model(source):
    source = source[: source.index("PBL_DMA_CONTROLLER_STATE_DEFINE")]
    source = re.sub(
        r"#define DMA_REG\(offset\).*",
        "#define DMA_REG(offset) registers[(offset)/4]",
        source,
    )
    source = re.sub(
        r"#define SYSTEM_REG\(offset\).*",
        "#define SYSTEM_REG(offset) system_regs[(offset)/4]",
        source,
    )
    source = source.replace(
        "DMA_CHANNEL_REG(channel_index, DMA_CHANNEL_STATUS) = DMA_STATUS_INTERRUPTS;",
        "DMA_CHANNEL_REG(channel_index, DMA_CHANNEL_STATUS) = 0;",
    )
    source = source.replace(
        "DMA_CHANNEL_REG(channel_index, DMA_CHANNEL_STATUS) = status;",
        "DMA_CHANNEL_REG(channel_index, DMA_CHANNEL_STATUS) &= ~status;",
    )
    return source


class GdmaTest(unittest.TestCase):
    def test_native_single_cyclic_errors_termination_and_channel_isolation(self):
        run("drivers/dma/t5ai.c", PREFIX, MAIN, transform=model)

    def test_retained_owners_hold_cyclic_and_release_after_stop(self):
        header = f'#include "{ROOT / "include/pbl/soc/t5ai/sleep.h"}"\n'
        spies = r"""
static unsigned held,faults;
static void t5ai_sleep_block(T5AISleepOwner owner){assert(owner==T5AISleepOwner_DMA);++held;}
static void t5ai_sleep_unblock(T5AISleepOwner owner){assert(owner==T5AISleepOwner_DMA && held);--held;}
static void t5ai_sleep_fault(void){++faults;}
""".replace("static void t5ai_sleep_", "void t5ai_sleep_")
        main = MAIN.replace(
            "assert(CH(c,8)==0x60024000);",
            "assert(held==1);assert(CH(c,8)==0x60024000);",
        )
        main = main.replace(
            "assert(callbacks==previous+1 && !last_status && CH(c,0));",
            "assert(held==1);assert(callbacks==previous+1 && !last_status && CH(c,0));",
        )
        main = main.replace(
            "assert(!depth && registers",
            "assert(!held && faults==8);assert(!depth && registers",
        )
        run(
            "drivers/dma/t5ai.c",
            PREFIX + header + spies,
            main,
            defines=("CONFIG_T5AI_RETAINED_SLEEP",),
            transform=model,
        )

    def test_consumers_use_native_dma(self):
        for path in (
            "drivers/spi/t5ai.c",
            "drivers/t5ai/sd.c",
            "drivers/mic/t5ai/adc.c",
        ):
            source = (ROOT / path).read_text()
            self.assertIn("pbl_dma_", source)
            self.assertNotIn("0x45020000", source)
            self.assertNotIn("t5ai_gdma_irq_", source)
