# SPDX-License-Identifier: Apache-2.0
"""Execute native audio period delivery, overrun, settling and termination."""

import unittest

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_native_objects import body

PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define T5AI_MIC_DMA_HALF_SAMPLES 320u
#define DMA_BYTES 2560u
#define DMA_HALF_TICKS 640u
#define AUD_CONFIG 0u
#define AUD_FIFO_STATUS 1u
#define AUD_ADC_ENABLE 1u
#define AUD_ADC_FULL 2u
#define __DSB() ((void)0)
struct pbl_audio_dai {int unused;};
struct pbl_audio_stream {size_t period_frames;unsigned periods,rate,channels;};
struct pbl_audio_state {struct pbl_audio_stream stream;};
struct pbl_audio {struct pbl_audio_state *state;};
struct pbl_audio_t5ai_state {bool running,faulted,powered;uint64_t dma_irq_ticks,settle_deadline;uint32_t dropped_samples,fifo_full_events;uint32_t *dma_buffer;size_t period_frames;unsigned period_index;};
struct pbl_audio_t5ai {struct pbl_audio_t5ai_state *state;struct pbl_audio_dai dai;};
static struct pbl_audio_state card_state;
static struct pbl_audio card={&card_state};
static int16_t periods[3][640],s_completed[320];
static unsigned completed,xruns,hw;
static const struct pbl_audio *pbl_audio_dai_card(const struct pbl_audio_dai *d){return &card;}
static void *pbl_audio_period(const struct pbl_audio *a,unsigned idx){assert(idx<3);return periods[idx];}
static void pbl_audio_period_elapsed(const struct pbl_audio *a){++completed;hw=(hw+1)%a->state->stream.periods;}
static void pbl_audio_report_xrun(const struct pbl_audio *a){++xruns;}
static const int s_dma;
static size_t remaining;static uint64_t ticks;static bool change_half;
static unsigned stopped,reads;static uint32_t regs[2];
static int pbl_dma_tx_status(const int *dma,size_t *residue){*residue=remaining;if(change_half && ++reads==1)remaining=DMA_BYTES;return 0;}
static uint64_t t5ai_rtc_get_raw_ticks(void){return ticks;}
static uint32_t *prv_reg(const struct pbl_audio_t5ai *d,unsigned offset){return &regs[offset];}
static void prv_dma_stop(void){++stopped;}
static void pbl_dcache_invalidate_range(void *p,size_t n){assert(n==DMA_BYTES/2);}
"""
MAIN = r"""
int main(void){
 uint32_t words[640];for(unsigned i=0;i<640;++i)words[i]=0x7fff0000u+i;
 for(unsigned frames=160;frames<=640;frames*=2){
  struct pbl_audio_t5ai_state state={.running=true,.dma_buffer=words,.settle_deadline=640};
  struct pbl_audio_t5ai dev={.state=&state};card_state.stream=(struct pbl_audio_stream){.period_frames=frames,.periods=3};
  completed=hw=xruns=0;memset(periods,0,sizeof(periods));
  ticks=640;remaining=1280;prv_dma_done(&dev,0);assert(!completed && !state.period_frames);
  ticks+=640;remaining=1280;prv_dma_done(&dev,0);
  ticks+=640;remaining=2560;prv_dma_done(&dev,0);
  assert(completed==640/frames && state.period_index==hw && !xruns);
  // The class ring wraps at arbitrary native stream period boundaries.
  for(unsigned i=0;i<640;++i){unsigned idx=(i/frames)%3;unsigned at=i%frames;
   if(i/frames+3>=640/frames)assert(periods[idx][at]==(int16_t)i);}
  unsigned before=completed;size_t partial=state.period_frames;
  change_half=true;reads=0;remaining=1280;ticks+=640;prv_dma_done(&dev,0);
  assert(completed==before && state.period_frames==0 && xruns==1 && state.dropped_samples==320);
  (void)partial;change_half=false;
  ticks+=1280;remaining=1280;prv_dma_done(&dev,0);assert(xruns==2 && state.dropped_samples==640);
  regs[0]=AUD_ADC_ENABLE;prv_dma_done(&dev,-EIO);assert(state.faulted && !regs[0] && xruns==3);
  state.running=false;before=completed;prv_dma_done(&dev,0);assert(completed==before);
 }
 assert(stopped==3);
 return 0;
}
"""


class MicrophoneDmaTest(unittest.TestCase):
    def test_native_periods_settling_late_copy_and_error(self):
        run(
            "drivers/audio/t5ai_mic.c",
            PREFIX,
            MAIN,
            transform=lambda source: (
                body(source, "prv_dma_writing_second") + body(source, "prv_dma_done")
            ),
        )


LIFECYCLE_PREFIX = (
    PREFIX
    + r"""
#include <stdlib.h>
#define MIC_SETTLE_TICKS 6400u
#define AUD_FIFO_CONFIG 1u
#define AUD_FIFO_THRESHOLD 8u
static struct pbl_audio_t5ai_state capture;
static struct pbl_audio_t5ai device={.state=&capture};
static unsigned allocations,frees,powerups,powerdowns,dma_starts,irq_locked;
static bool alloc_fail,power_fail,stop_fail;
static int dma_error;
static const struct pbl_audio_t5ai *prv_dev(const struct pbl_audio_dai *d){return &device;}
static void *pbl_audio_buf_alloc(size_t n){assert(n==DMA_BYTES);++allocations;return alloc_fail?NULL:malloc(n);}
static void pbl_audio_buf_free(void *p){if(p){++frees;free(p);}}
static bool prv_power_up(const struct pbl_audio_t5ai *d){++powerups;d->state->powered=!power_fail;return !power_fail;}
static bool prv_power_down(const struct pbl_audio_t5ai *d){assert(!d->state->running);++powerdowns;d->state->powered=false;return !stop_fail;}
static int prv_dma_start(const struct pbl_audio_t5ai *d){assert(d->state->dma_buffer && d->state->powered);++dma_starts;return dma_error;}
static void pbl_irq_lock(void){assert(!irq_locked);irq_locked=1;}
static void pbl_irq_unlock(void){assert(irq_locked);irq_locked=0;}
"""
)
LIFECYCLE_MAIN = r"""
int main(void){
 struct pbl_audio_stream stream={.rate=16000,.channels=1,.period_frames=320,.periods=3};
 stream.rate=8000;assert(prv_start(&device.dai,&stream,0,false)==-EINVAL);stream.rate=16000;
 stream.channels=2;assert(prv_start(&device.dai,&stream,0,false)==-EINVAL);stream.channels=1;
 stream.period_frames=0;assert(prv_start(&device.dai,&stream,0,false)==-EINVAL);stream.period_frames=320;
 stream.periods=1;assert(prv_start(&device.dai,&stream,0,false)==-EINVAL);stream.periods=3;
 assert(prv_start(&device.dai,&stream,1,false)==-EINVAL);
 assert(prv_start(&device.dai,&stream,0,true)==-EINVAL);
 assert(!allocations && !powerups);
 alloc_fail=true;assert(prv_start(&device.dai,&stream,0,false)==-ENOMEM);
 assert(!capture.dma_buffer && !powerups);alloc_fail=false;
 power_fail=true;assert(prv_start(&device.dai,&stream,0,false)==-EIO);
 assert(frees==1 && !capture.dma_buffer && !dma_starts);power_fail=false;
 dma_error=-EBUSY;assert(prv_start(&device.dai,&stream,0,false)==-EBUSY);
 assert(frees==2 && powerdowns==1 && !capture.dma_buffer && !capture.running);
 dma_error=0;ticks=123;assert(prv_start(&device.dai,&stream,0,false)==0);
 assert(capture.running && capture.powered && capture.settle_deadline==6523 && !irq_locked);
 assert(capture.period_index==0 && capture.period_frames==0 && regs[0]==AUD_ADC_ENABLE);
 unsigned before=allocations;assert(prv_start(&device.dai,&stream,0,false)==-EBUSY);
 assert(allocations==before);
 assert(prv_stop(&device.dai)==0 && !capture.running && !capture.dma_buffer && frees==3);
 assert(prv_start(&device.dai,&stream,0,false)==0);stop_fail=true;
 assert(prv_stop(&device.dai)==-EIO && !capture.running && !capture.dma_buffer && frees==4);
 capture.faulted=true;before=allocations;
 assert(prv_start(&device.dai,&stream,0,false)==-EIO && allocations==before);
 assert(!irq_locked);
 return 0;
}
"""


class MicrophoneLifecycleTest(unittest.TestCase):
    def test_native_stream_validation_cleanup_restart_and_fault(self):
        run(
            "drivers/audio/t5ai_mic.c",
            LIFECYCLE_PREFIX,
            LIFECYCLE_MAIN,
            transform=lambda source: (
                body(source, "prv_start") + body(source, "prv_stop")
            ),
        )
