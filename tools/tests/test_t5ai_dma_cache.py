# SPDX-License-Identifier: Apache-2.0
"""Verify native cache maintenance on inherited T5AI D-cache state."""

import unittest

from tools.tests.native_t5ai_harness import run

PREFIX = r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define SCB_CCR_DC_Msk 1u
static struct {uint32_t CCR,DCCMVAC,DCIMVAC,DCCIMVAC;} scb;
#define SCB (&scb)
static unsigned barriers,count;static uintptr_t addresses[128];static volatile uint32_t *operation;
#define __DSB() (++barriers)
#define __ISB() (++barriers)
static void record(volatile uint32_t *op,uintptr_t address){assert(count<128);if(!count)operation=op;else assert(operation==op);addresses[count++]=address;}
"""
MAIN = r"""
int main(void){
 scb.CCR=0;dcache_flush((void *)0x28010001,65);assert(!count && !barriers);
 scb.CCR=1;dcache_flush((void *)0x28010001,65);
 assert(count==3 && barriers==3 && operation==&scb.DCCMVAC);
 assert(addresses[0]==0x28010000 && addresses[1]==0x28010020 && addresses[2]==0x28010040);
 count=barriers=0;dcache_flush((void *)0x28010000,0);assert(!count && !barriers);
 dcache_invalidate((void *)0x28010020,32);assert(count==1 && addresses[0]==0x28010020 && operation==&scb.DCIMVAC);
 count=0;dcache_flush_invalidate((void *)0x60000000,1864);assert(count==59 && operation==&scb.DCCIMVAC);
 for(unsigned i=0;i<count;++i)assert(addresses[i]==0x60000000u+i*32u);
 return 0;
}
"""


class DmaCacheTest(unittest.TestCase):
    def test_cache_state_line_alignment_and_every_line(self):
        run(
            "soc/t5ai/cache.c",
            PREFIX,
            MAIN,
            transform=lambda source: source.replace("*op = line;", "record(op, line);"),
        )
