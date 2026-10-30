# SPDX-License-Identifier: Apache-2.0
"""Execute the shared route and microphone lifecycle in all board IRQ combinations."""
import itertools
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|bool) '+name+r'\(', source, re.M)
    start=source.index('{',match.start())
    end=start+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    return source[match.start():end]+'\n'


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define PBL_FOREVER 0
#define PBL_LOG_DBG(...) ((void)0)
#define PBL_LOG_ERR(...) ((void)0)
#define FAILED(x) ((x)!=0)
#define MIC_DEFAULT_VOLUME 0
#define AUD_ADC_GAIN_DEFAULT 63
#define AUD_CONFIG 0
#define AUD_FIFO_CONFIG 1
#define AUD_ADC_ENABLE 40
#define AUD_FIFO_THRESHOLD 8
#define SYS_MIC1_INDEX 19
#define SYS_AUDIO_BIAS_INDEX 18
#define SYS_MIC1_DEFAULT 0
#define SYS_AUDIO_BIAS_DEFAULT 0
#define T5AI_MIC_RING_SAMPLES 128
#define DMA_CTRL 0
#define DMA_STATUS 1
#define DMA_IRQ_STATUS (7u<<18)
#define DMA_BYTES 256
#define DMA_CACHE_ALIGNMENT 32
#define MIC_SETTLE_TICKS 200
#define SYS_DMA_IRQ (1u<<11)
#define GDMA_IRQn 11
#define SYS_CPU1_IRQ_ENABLE route
#define SYSTEM_CPU1_INTERRUPT_ENABLE route
static uint32_t route, audio[2], dma[2], pending;
static bool nvic, analog_ok=true;
static unsigned locks, clears, dma_stops;
static uint32_t *prv_dma(unsigned index){return &dma[index];}
static void pbl_irq_lock(void){++locks;}
static void pbl_irq_unlock(void){assert(locks);--locks;}
static void NVIC_DisableIRQ(int irq){assert(irq==11);nvic=false;}
static void NVIC_EnableIRQ(int irq){assert(irq==11);nvic=true;}
static unsigned NVIC_GetEnableIRQ(int irq){assert(irq==11);return nvic;}
static void NVIC_ClearPendingIRQ(int irq){assert(irq==11);++clears;pending=0;}
static void NVIC_SetPriority(int irq,unsigned priority){assert(irq==11 && priority==6);}
static void __DSB(void){}
typedef int status_t;
typedef void (*MicDataHandlerCB)(void);
typedef struct {
 bool initialized,running,faulted,powered,pending;
 int mutex,gain;
 void *dma_allocation;uint32_t *dma_buffer;int16_t *ring,*buffer;
 size_t buffer_len,read_index,write_index,count;
 unsigned dropped_samples,fifo_full_events;
 uint64_t settle_deadline,dma_irq_ticks;
 MicDataHandlerCB handler;void *context;
} MicDeviceState;
typedef struct {MicDeviceState *state;int irq;unsigned irq_priority;} MicDevice;
static MicDeviceState state;
static MicDevice device={&state,11,6};
static MicDevice *MIC=&device;
static int16_t s_mono_ring[128];
static uint32_t *prv_reg(MicDevice *d,unsigned index){(void)d;return &audio[index];}
static void pbl_mutex_init(int *m){*m=0;}
static void pbl_mutex_lock(int *m,int timeout){(void)timeout;assert(!*m);*m=1;}
static void pbl_mutex_unlock(int *m){assert(*m);*m=0;}
static void *kernel_malloc(size_t n){return malloc(n);}
static void kernel_free(void *p){free(p);}
static bool prv_analog_write(unsigned index,uint32_t v){(void)index;(void)v;assert(!dma[0]);return analog_ok;}
static status_t t5ai_cp_mailbox_set_audio_clock(bool on){assert(!on);return 0;}
static status_t t5ai_cp_mailbox_set_audio_power(bool on){assert(!on);return 0;}
static bool prv_power_up(MicDevice *d){d->state->powered=true;return true;}
static void prv_dma_start(MicDevice *d){(void)d;dma[0]=1;}
static uint64_t t5ai_rtc_get_raw_ticks(void){return 1000;}
static uint32_t t5ai_interrupt_profile_irq_enter(void){return 0;}
static void t5ai_interrupt_profile_irq_exit(unsigned i,uint32_t n){(void)i;(void)n;}
static void display_co5300_dma_irq(void){pending&=~1u;}
static void t5ai_sd_dma_irq(void){pending&=~12u;}
static void t5ai_mic_dma_irq_handler(MicDevice *d){(void)d;pending&=~2u;}
#define PBL_IRQ_CONNECT(...)
static void handler(void){}
'''

MAIN = r'''
static void check(unsigned active) {
 assert(locks==0);
 if(active)assert(nvic && (route&SYS_DMA_IRQ));
 assert((route&~SYS_DMA_IRQ)==0x80000000u);
 assert(clears==0);
}
static void dispatch(void){
#if defined(CONFIG_T5AI_DISPLAY_DMA_IRQ) || defined(CONFIG_T5AI_SD_PFS)
 prv_shared_gdma_irq();
#elif defined(CONFIG_MIC)
 t5ai_mic_dma_irq_handler(MIC);
#endif
}
int main(void){
 unsigned clients[]={DISPLAY,MIC_ON,SD};
 for(unsigned inherited=0;inherited<4;++inherited){
  nvic=inherited&1;route=0x80000000u|((inherited&2)?SYS_DMA_IRQ:0);
  state=(MicDeviceState){0};dma[0]=0;pending=0;
  assert(nvic==(bool)(inherited&1));
  unsigned active=0;
  for(unsigned i=0;i<3;++i){
   unsigned client=clients[ORDER[i]];if(!client)continue;
   pending=active; // Initializing a new client must preserve in-flight peers.
   if(client==2){mic_init(MIC);check(active);assert(pending==active);assert(mic_start(MIC,handler,NULL,s_mono_ring,16));}
   else {t5ai_gdma_irq_acquire((T5aiGdmaOwner)client);t5ai_gdma_irq_acquire((T5aiGdmaOwner)client);}
   active|=client;check(active);assert(pending==(active&~client));
   if(MIC_ON)mic_init(MIC);check(active);
  }
  for(unsigned repeat=0;repeat<20;++repeat){
   pending=active|0xf000u;dispatch();assert(pending==0xf000u); // CP flags survive AP dispatch.
   if(MIC_ON){
    pending=active&~2u;analog_ok=(repeat%2)==0;
    mic_stop(MIC);active&=~2u;check(active);assert(pending==active);
    assert(dma[0]==0 && dma[1]==DMA_IRQ_STATUS && !state.dma_allocation);
    mic_stop(MIC);check(active); // Idempotent shutdown.
    state.faulted=false;
    assert(mic_start(MIC,handler,NULL,s_mono_ring,16));active|=2;
    check(active);assert(pending==(active&~2u));
   }
  }
  analog_ok=true;
  if(MIC_ON){mic_stop(MIC);active&=~2u;check(active);}
  if(DISPLAY){t5ai_gdma_irq_release(T5AI_GDMA_DISPLAY);active&=~1u;check(active);}
  if(SD){t5ai_gdma_irq_release(T5AI_GDMA_SD);active&=~12u;check(active);}
  assert(nvic==(bool)(inherited&1));
  assert((bool)(route&SYS_DMA_IRQ)==(bool)(inherited&2));
  t5ai_gdma_irq_release(T5AI_GDMA_MIC); // Release without claim does nothing.
 }
}
'''


class GdmaTest(unittest.TestCase):
    def test_matrix_lifecycle_and_initialization_orders(self):
        owner=(ROOT/'soc/t5ai/gdma.c').read_text()
        owner='\n'.join(line for line in owner.splitlines() if not line.startswith('#include') and not line.startswith('#define SYSTEM_CPU1_INTERRUPT_ENABLE'))
        header=(ROOT/'include/pbl/soc/t5ai/gdma.h').read_text().replace('#pragma once','')
        mic=(ROOT/'drivers/mic/t5ai/adc.c').read_text()
        lifecycle=''.join(function(mic,n) for n in ['prv_dma_stop','prv_power_down','mic_init','mic_start','mic_stop'])
        board=(ROOT/'fw/board/boards/board_unhygienix.c').read_text()
        dispatch=function(board,'prv_shared_gdma_irq')
        dispatch='#if defined(CONFIG_T5AI_DISPLAY_DMA_IRQ) || defined(CONFIG_T5AI_SD_PFS)\n'+dispatch+'#endif\n'
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            for mic_on,sd,display in itertools.product([0,1],repeat=3):
                with self.subTest(mic=mic_on,sd=sd,display=display):
                    config=''.join(f'#define {name} 1\n' for name,on in [('CONFIG_MIC',mic_on),('CONFIG_T5AI_SD_PFS',sd),('CONFIG_T5AI_DISPLAY_DMA_IRQ',display)] if on)
                    for order in itertools.permutations(range(3)):
                        setup=f'\n#define DISPLAY {display}\n#define MIC_ON {mic_on*2}\n#define SD {sd*12}\nstatic unsigned ORDER[3]={{{",".join(map(str,order))}}};\n'
                        (p/'test.c').write_text(config+PREFIX+header+owner+'\n'+lifecycle+dispatch+setup+MAIN)
                        subprocess.run(['cc','-std=c11','-Werror=implicit-function-declaration',str(p/'test.c'),'-o',str(p/'test')],check=True,capture_output=True)
                        result = subprocess.run([str(p/'test')],capture_output=True,text=True)
                        self.assertEqual(result.returncode,0,result.stderr)

    def test_driver_claims_and_channel_local_acknowledgement(self):
        mic=(ROOT/'drivers/mic/t5ai/adc.c').read_text()
        display=(ROOT/'drivers/display/t5ai/display_co5300.c').read_text()
        sd=(ROOT/'drivers/t5ai/sd.c').read_text()
        self.assertIn('t5ai_gdma_irq_acquire(T5AI_GDMA_DISPLAY)',display)
        self.assertIn('t5ai_gdma_irq_acquire(T5AI_GDMA_SD)',sd)
        self.assertNotIn('NVIC_',mic)
        self.assertLess(function(mic,'prv_power_down').index('prv_dma_stop'),function(mic,'prv_power_down').index('t5ai_gdma_irq_release'))
        self.assertIn('*prv_dma(DMA_STATUS) = status;',mic)
        self.assertIn('DMA_STATUS = status;',display)
        self.assertIn('CH(channel, DMA_CHANNEL_STATUS) = status;',sd)
