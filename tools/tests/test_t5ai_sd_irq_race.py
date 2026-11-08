# SPDX-License-Identifier: Apache-2.0
"""Check interrupt exclusion while SD write setup shares a mask with DMA IRQ."""

import tempfile
import unittest
from pathlib import Path

from tools.tests import test_t5ai_sd_pfs as model

PREFIX = r"""
#include "common.h"
static unsigned irq_depth, setup_checks, dma_injections;
static bool write_armed, dma_pending;
static void test_gate_mask(void);
static void test_lock(void) { ++irq_depth; }
static void test_unlock(void);
#define pbl_irq_lock test_lock
#define pbl_irq_unlock test_unlock
"""

HOOKS = r"""
static void inject_dma(void) {
 dma_pending=false;write_armed=false;
 SD(SD_INTERRUPT_STATUS)=0;
 prv_dma_done((void *)1,0);
 ++dma_injections;
}
static void test_gate_mask(void) {
 // Schedule the callback between the volatile load and stale-value store.
 const uint32_t before=SD(SD_INTERRUPT_MASK);
 const bool armed=write_armed;
 if(armed) {
  ++setup_checks;
  if(irq_depth)dma_pending=true;
  else inject_dma();
 }
 SD(SD_INTERRUPT_MASK)=before|SD_TX_FIFO_CLOCK_GATE;
 if(armed && !irq_depth)assert(SD(SD_INTERRUPT_MASK)&TX_EMPTY);
}
static void test_unlock(void) {
 assert(irq_depth);
 if(--irq_depth || !dma_pending)return;
 // Deliver the pending callback at the first legal boundary after setup.
 inject_dma();
 assert(SD(SD_INTERRUPT_MASK)&TX_EMPTY);
 assert(SD(SD_INTERRUPT_MASK)&SD_TX_FIFO_CLOCK_GATE);
 assert(SD(SD_DATA_CONTROL)&(1u<<16));
}
"""


class SDMaskRaceTest(unittest.TestCase):
    def test_write_setup_excludes_dma_callback(self):
        source = (
            (model.ROOT / "drivers/t5ai/sd.c")
            .read_text()
            .replace(
                "SD(SD_INTERRUPT_MASK) |= SD_TX_FIFO_CLOCK_GATE;",
                "test_gate_mask();",
            )
        )
        harness = PREFIX + model.DRIVER.replace(
            "CH(n,DMA_CHANNEL_STATUS)=0;return 0;",
            "CH(n,DMA_CHANNEL_STATUS)=0;"
            "if(dir==PBL_DMA_MEM_TO_DEV)write_armed=true;return 0;",
        )
        # This case delivers the callback early; the base suite separately
        # covers SD-before-DMA and intermediate FIFO-empty ordering.
        harness = harness.replace("completion_order=1;", "completion_order=0;")
        harness = harness.replace("completion_order=2;", "completion_order=0;")
        harness = harness.replace("int main(void) {", HOOKS + "\nint main(void) {")
        harness = harness.replace(
            'puts("PASS: SD init/CSD,',
            "assert(setup_checks && dma_injections==3 && !irq_depth);"
            'puts("PASS: SD init/CSD,',
        )
        with tempfile.TemporaryDirectory() as directory:
            driver = Path(directory) / "sd.c"
            driver.write_text(source)
            for four_bit in (False, True):
                with self.subTest(four_bit=four_bit):
                    model.SDTests().compile_run(driver, harness, four_bit=four_bit)
