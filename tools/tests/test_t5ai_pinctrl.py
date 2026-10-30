# SPDX-License-Identifier: Apache-2.0
"""Verify native pin mux encoding, pad preservation and invalid state atomicity."""

import re
import unittest

from tools.tests.native_t5ai_harness import ROOT, run

PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#define nullptr NULL
struct pbl_pinctrl_controller {uint16_t num_pads;};
struct pbl_pinctrl_controller_ops {int (*configure)(const struct pbl_pinctrl_controller *,const uint32_t *,uint8_t,uintptr_t);int (*get)(const struct pbl_pinctrl_controller *,uint16_t,uint32_t *);};
static uint32_t pads[56],mux[7];static unsigned depth;
static void pbl_irq_lock(void){++depth;}static void pbl_irq_unlock(void){assert(depth);--depth;}
"""
MAIN = r"""
int main(void){
 const struct pbl_pinctrl_controller ctrl={56};
 for(unsigned i=0;i<56;++i)pads[i]=0xdeadffff;
 for(unsigned i=0;i<7;++i)mux[i]=0xaaaaaaaa;
 const uint32_t pins[]={T5AI_PIN(20,0,T5AI_PIN_PULL_UP),T5AI_PIN(21,0,T5AI_PIN_PULL_UP),T5AI_PIN(22,3,T5AI_PIN_DRIVE(3))};
 assert(prv_configure(&ctrl,pins,3,0)==0 && !depth);
 assert(pads[20]==((0xdeadffff&~0x3fff)|0x78));
 assert(pads[22]==((0xdeadffff&~0x3fff)|0x348));
 assert(mux[2]==0xa300aaaa);assert(pads[19]==0xdeadffff && mux[1]==0xaaaaaaaa);
 uint32_t value;assert(prv_get(&ctrl,20,&value)==0 && value==pads[20]);
 assert(prv_get(&ctrl,56,&value)==-EINVAL && prv_get(&ctrl,0,NULL)==-EINVAL);
 uint32_t before[56];memcpy(before,pads,sizeof pads);
 const uint32_t invalid[]={T5AI_PIN(20,1,0),T5AI_PIN(56,0,0)};
 assert(prv_configure(&ctrl,invalid,2,0)==-EINVAL && !memcmp(before,pads,sizeof pads));
 return 0;
}
"""


def model(source):
    source = source[: source.index("PBL_DEVICE_STATE_DEFINE")]
    source = re.sub(r"#define PAD_BASE .*", "#define PAD_BASE pads", source)
    return re.sub(r"#define MUX_BASE .*", "#define MUX_BASE mux", source)


class PinctrlTest(unittest.TestCase):
    def test_pin_encoding_neighbor_preservation_and_invalid_states(self):
        header = (ROOT / "include/pbl/drivers/pinctrl/t5ai.h").read_text()
        header = re.sub(r"^#(?:include|pragma).*", "", header, flags=re.MULTILINE)
        header = header[: header.index("extern const")]
        run("drivers/pinctrl/t5ai.c", PREFIX + header, MAIN, transform=model)
