# SPDX-License-Identifier: Apache-2.0
"""Execute production DMA against read-only inherited global policy and AP-only registers."""

import unittest

from tools.tests.native_t5ai_harness import ROOT, run
from tools.tests.test_t5ai_gdma import MAIN, PREFIX, model

GUARDS = r"""
#include <sys/mman.h>
#include <unistd.h>
static uint32_t *global_page;
static unsigned channel_accesses, irqs, cp_change;
static volatile uint32_t *dma_register(unsigned offset){
 if(offset==8)return global_page;
 if(offset>=0x40){assert((offset-0x40)/0x40<4);++channel_accesses;}
 if(offset==0x10 && cp_change){
  assert(mprotect(global_page,4096,PROT_READ|PROT_WRITE)==0);
  *global_page|=2;assert(mprotect(global_page,4096,PROT_READ)==0);cp_change=0;
 }
 return &registers[offset/4];
}
static void setup(unsigned value){
 global_page=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(global_page!=MAP_FAILED);*global_page=value;
 assert(mprotect(global_page,4096,PROT_READ)==0);
 registers[4]=0xa00;registers[5]=0xb00;registers[10]=0xf000;
}
"""


def guarded(source):
    return model(source).replace(
        "#define DMA_REG(offset) registers[(offset)/4]",
        "#define DMA_REG(offset) (*dma_register(offset))",
    )


def prefix():
    return PREFIX.replace("assert(irq==11);}", "assert(irq==11);++irqs;}").replace(
        "static void NVIC_SetPriority",
        "static unsigned irqs;\nstatic void NVIC_SetPriority",
    ) + GUARDS.replace("channel_accesses, irqs,", "channel_accesses,")


class DmaClockPolicyTest(unittest.TestCase):
    def test_explicit_opt_out_forces_bypass_preserving_other_bits(self):
        run(
            "drivers/dma/t5ai.c",
            PREFIX,
            r"""
int main(void){registers[2]=0xa5a50000;assert(prv_init(NULL)==0);
 assert(registers[2]==0xa5a50003 && !depth);return 0;}
""",
            transform=model,
        )

    def test_reset_rejection_precedes_channel_and_irq_setup(self):
        run(
            "drivers/dma/t5ai.c",
            prefix(),
            r"""
int main(void){
 for(unsigned value=0;value<=2;value+=2){
  setup(value);assert(prv_init(NULL)==-EAGAIN);
  assert(!channel_accesses && !irqs);
  assert(registers[4]==0xa00 && registers[5]==0xb00 && registers[10]==0xf000);
  assert(*global_page==value && munmap(global_page,4096)==0);
 }
 assert(!depth);return 0;}
""",
            defines=("CONFIG_T5AI_DMA_INHERIT_CLOCK_POLICY",),
            transform=guarded,
        )

    def test_all_transfer_error_cancel_paths_cannot_write_inherited_policy(self):
        for value in (1, 3, 0xA5A50005, 0xA5A50007):
            main = MAIN.replace("int main(void){", f"int main(void){{setup({value}u);")
            main = main.replace(
                "return 0;",
                f"assert(*global_page=={value}u);assert(munmap(global_page,4096)==0);return 0;",
            )
            with self.subTest(global_word=hex(value)):
                run(
                    "drivers/dma/t5ai.c",
                    prefix(),
                    main,
                    defines=("CONFIG_T5AI_DMA_INHERIT_CLOCK_POLICY",),
                    transform=guarded,
                )

    def test_cp_bypass_update_after_guard_is_never_overwritten(self):
        run(
            "drivers/dma/t5ai.c",
            prefix(),
            r"""
int main(void){setup(1);cp_change=1;assert(prv_init(NULL)==0);
 assert(*global_page==3 && !cp_change && registers[4]==0xa0f && registers[5]==0xb0f && registers[10]==0xf000);
 assert(munmap(global_page,4096)==0 && !depth);return 0;}
""",
            defines=("CONFIG_T5AI_DMA_INHERIT_CLOCK_POLICY",),
            transform=guarded,
        )

    def test_mixed_ap_owners_keep_blockers_through_cancel_and_error(self):
        header = f'#include "{ROOT / "include/pbl/soc/t5ai/sleep.h"}"\n'
        spies = r"""
static unsigned held,faults;
void t5ai_sleep_block(T5AISleepOwner o){assert(o==T5AISleepOwner_DMA);++held;}
void t5ai_sleep_unblock(T5AISleepOwner o){assert(o==T5AISleepOwner_DMA && held);--held;}
void t5ai_sleep_fault(void){++faults;}
"""
        run(
            "drivers/dma/t5ai.c",
            prefix() + header + spies,
            r"""
int main(void){
 setup(1);assert(prv_init(NULL)==0);
 for(unsigned c=0;c<4;++c){
  struct pbl_dma_chan ch={&T5AI_DMA,c,c,0};
  struct pbl_dma_slave_config cfg={.src_addr=0x47000044,.dst_addr=0x64000000,.src_addr_width=4,.dst_addr_width=4};
  channels[c].cfg=cfg;assert(prv_config(&T5AI_DMA,&ch,&cfg)==0);
  assert((c==1?prv_cyclic(&T5AI_DMA,c,(void*)0x28020000,2560,1280,PBL_DMA_DEV_TO_MEM):
    prv_single(&T5AI_DMA,c,(void*)0x28022000,512,c==3?PBL_DMA_DEV_TO_MEM:PBL_DMA_MEM_TO_DEV))==0);
 }
 assert(held==4);prv_terminate(&T5AI_DMA,0);assert(held==3);
 DMA_CHANNEL_REG(0,0x30)=DMA_STATUS_FINISH;pbl_dma_t5ai_irq_handler();assert(!callbacks && held==3);
 DMA_CHANNEL_REG(2,0x30)=DMA_STATUS_BUS_ERROR;pbl_dma_t5ai_irq_handler();assert(callbacks==1 && faults==1 && held==2);
 DMA_CHANNEL_REG(1,0x30)=DMA_STATUS_HALF;pbl_dma_t5ai_irq_handler();assert(callbacks==2 && held==2);
 DMA_CHANNEL_REG(3,0x30)=DMA_STATUS_FINISH;pbl_dma_t5ai_irq_handler();assert(callbacks==3 && held==1);
 prv_terminate(&T5AI_DMA,1);assert(!held && *global_page==1 && !depth);
 assert(munmap(global_page,4096)==0);return 0;
}
""",
            defines=(
                "CONFIG_T5AI_DMA_INHERIT_CLOCK_POLICY",
                "CONFIG_T5AI_RETAINED_SLEEP",
            ),
            transform=guarded,
        )


if __name__ == "__main__":
    unittest.main()
