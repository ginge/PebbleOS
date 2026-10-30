# SPDX-License-Identifier: Apache-2.0
"""Execute native microphone period handling, overrun and termination."""

import re
import unittest

from tools.tests.native_t5ai_harness import run

PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define T5AI_MIC_DMA_HALF_SAMPLES 320u
#define T5AI_MIC_RING_SAMPLES 2560u
#define DMA_BYTES 2560u
#define DMA_HALF_TICKS 655u
#define AUD_CONFIG 0u
#define AUD_FIFO_STATUS 1u
#define AUD_ADC_ENABLE 1u
#define AUD_ADC_FULL 2u
#define __DSB() ((void)0)
typedef struct {bool running,faulted,pending;uint64_t dma_irq_ticks,settle_deadline;uint32_t dropped_samples,fifo_full_events;uint32_t *dma_buffer;int16_t *ring;size_t count,write_index,buffer_len;} MicDeviceState;
typedef struct {MicDeviceState *state;} MicDevice;
static const int s_dma;
static size_t remaining;static uint64_t ticks;static bool change_half,queue_ok=true;
static unsigned stopped,queued,reads;static uint32_t regs[2];
static int pbl_dma_tx_status(const int *dma,size_t *residue){*residue=remaining;if(change_half && ++reads==1)remaining=DMA_BYTES;return 0;}
static uint64_t t5ai_rtc_get_raw_ticks(void){return ticks;}
static uint32_t *prv_reg(MicDevice *d,unsigned offset){return &regs[offset];}
static void prv_dma_stop(void){++stopped;}
static void dcache_invalidate(void *p,size_t n){assert(n==DMA_BYTES/2);}
static void prv_dispatch(void *arg){}
static bool system_task_add_callback_from_isr_droppable(void (*fn)(void *),void *arg){assert(fn==prv_dispatch);++queued;return queue_ok;}
"""
MAIN = r"""
int main(void){
 uint32_t words[640];int16_t ring[2560];for(unsigned i=0;i<640;++i)words[i]=0x7fff0000u+i;
 MicDeviceState state={.running=true,.dma_buffer=words,.ring=ring,.buffer_len=320};MicDevice dev={&state};
 ticks=655;remaining=1280;prv_dma_done(&dev,0);assert(state.count==320 && queued==1 && state.pending);
 for(unsigned i=0;i<320;++i)assert(ring[i]==(int16_t)i);
 ticks+=655;remaining=2560;prv_dma_done(&dev,0);assert(state.count==640 && queued==1);
 for(unsigned i=320;i<640;++i)assert(ring[i]==(int16_t)i);
 state.count=2560;ticks+=655;prv_dma_done(&dev,0);assert(state.dropped_samples==320);
 state.count=0;state.pending=false;change_half=true;reads=0;remaining=1280;ticks+=655;
 prv_dma_done(&dev,0);assert(state.count==0 && state.dropped_samples==640);
 change_half=false;queue_ok=false;ticks+=655;prv_dma_done(&dev,0);assert(state.count==320 && !state.pending && queued==2);
 regs[0]=AUD_ADC_ENABLE;prv_dma_done(&dev,-EIO);assert(state.faulted && stopped==1 && !regs[0]);
 state.running=false;unsigned before=state.count;prv_dma_done(&dev,0);assert(state.count==before);
 return 0;
}
"""


def model(source):
    def function(name):
        match = re.search(r"static (?:bool|void) " + name + r"\([^;]+?\) \{", source)
        start = match.start()
        pos = source.index("{", start) + 1
        depth = 1
        while depth:
            depth += (source[pos] == "{") - (source[pos] == "}")
            pos += 1
        return source[start:pos]

    return function("prv_dma_writing_second") + "\n" + function("prv_dma_done")


class MicrophoneDmaTest(unittest.TestCase):
    def test_native_period_samples_ring_overflow_late_copy_and_error(self):
        run("drivers/mic/t5ai/adc.c", PREFIX, MAIN, transform=model)
