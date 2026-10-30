# SPDX-License-Identifier: Apache-2.0
"""Run quad SPI command framing, DMA segmentation and timeout callbacks."""

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
#define nullptr NULL
#define QSPI0_IRQn 20
#define PBL_MSEC(x) (x)
#define PBL_DMA_MEM_TO_DEV 0
#define PBL_DMA_SLAVE_BUSWIDTH_4_BYTES 4
#define PBL_PINCTRL_STATE_DEFAULT 0
#define PBL_SPI_MODE_0 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct pbl_device {int unused;};
struct pbl_dma_controller {struct pbl_device dev;};
struct pbl_dma_chan {const struct pbl_dma_controller *ctrl;};
struct pbl_dma_slave_config {uintptr_t dst_addr;unsigned dst_addr_width;};
struct pbl_pinctrl {int unused;};
struct pbl_spi_bus;
struct pbl_spi_xfer {const void *tx_buf;void *rx_buf;uint32_t len;uint8_t tx_nbits;};
struct pbl_spi_bus_state {const struct pbl_spi_xfer *xfers;size_t idx,num_xfers;};
struct pbl_spi_bus {struct pbl_spi_bus_state *state;};
struct pbl_spi_dev {uint32_t mode,max_hz;};
struct pbl_spi_t5ai {struct pbl_spi_bus bus;struct pbl_pinctrl pinctrl;struct pbl_dma_chan dma;};
struct pbl_spi_bus_ops {int (*init)(const struct pbl_spi_bus *);int (*configure)(const struct pbl_spi_bus *,const struct pbl_spi_dev *);void (*enable)(const struct pbl_spi_bus *);void (*disable)(const struct pbl_spi_bus *);int (*start)(const struct pbl_spi_bus *,const struct pbl_spi_xfer *);};
struct pbl_timer {void (*fn)(struct pbl_timer *);bool active;};
static uint32_t regs[256],sys[128];
static unsigned completions,terminated,submits,depth;static bool pending;
static unsigned drain_steps;
static int result;static size_t dma_len;static void *dma_buf,*dma_arg;static void (*dma_cb)(void *,int);
static void pbl_irq_lock(void){++depth;}static void pbl_irq_unlock(void){assert(depth);--depth;}
static void pbl_timer_init(struct pbl_timer *t,void (*fn)(struct pbl_timer *)){t->fn=fn;t->active=false;}
static void pbl_timer_start(struct pbl_timer *t,unsigned ms){assert(ms==20);t->active=true;}
static void pbl_timer_stop(struct pbl_timer *t){t->active=false;}
static int t5ai_cp_mailbox_enable_qspi0(void){return 0;}
static int pbl_pinctrl_select_state(const struct pbl_pinctrl *p,int state){return 0;}
static int pbl_device_init(const struct pbl_device *d){return 0;}
static int pbl_dma_slave_config(const struct pbl_dma_chan *c,const struct pbl_dma_slave_config *cfg){assert(cfg->dst_addr==0x64000000 && cfg->dst_addr_width==4);return 0;}
static int pbl_dma_prep_slave_single(const struct pbl_dma_chan *c,void *buf,size_t len,int dir,void (*cb)(void *,int),void *arg){
 assert(dir==0 && len && len<=65536);++submits;dma_buf=buf;dma_len=len;dma_cb=cb;dma_arg=arg;return 0;
}
static int pbl_dma_terminate(const struct pbl_dma_chan *c){++terminated;return 0;}
static void dcache_flush(void *buf,size_t len){}
static void delay_us(unsigned us){if(drain_steps && --drain_steps==0)regs[0x70/4]=(1u<<2)|(1u<<16);}
static void NVIC_SetPriority(int irq,unsigned priority){assert(irq==20 && priority==6);}
static void NVIC_EnableIRQ(int irq){assert(irq==20);}
static void NVIC_SetPendingIRQ(int irq){assert(irq==20);pending=true;}
static int prv_start(const struct pbl_spi_bus *,const struct pbl_spi_xfer *);
static void prv_disable(const struct pbl_spi_bus *);
static void pbl_spi_bus_complete(const struct pbl_spi_bus *bus,int status){
 pending=false;
 if(!status && ++bus->state->idx<bus->state->num_xfers){status=prv_start(bus,&bus->state->xfers[bus->state->idx]);if(!status)return;}
 prv_disable(bus);++completions;result=status;
}
"""
MAIN = r"""
int main(void){
 struct pbl_spi_bus_state state={0};const struct pbl_dma_controller ctrl={0};
 const struct pbl_spi_t5ai dev={.bus={&state},.dma={&ctrl}};const struct pbl_spi_bus *bus=&dev.bus;
 assert(prv_init(bus)==0);struct pbl_spi_dev peripheral={.max_hz=80000000};
 assert(prv_configure(bus,&peripheral)==0 && ((SYS(0x24)>>6)&15)==5);
 peripheral.mode=1;assert(prv_configure(bus,&peripheral)==-ENOTSUP);peripheral.mode=0;
 static uint8_t pixels[65536+1864] __attribute__((aligned(32)));
 const uint8_t header[]={0x32,0,0x2c,0};
 const struct pbl_spi_xfer xfers[]={{.tx_buf=header,.len=4},{.tx_buf=pixels,.len=sizeof pixels,.tx_nbits=4}};
 state=(struct pbl_spi_bus_state){xfers,0,2};REG(0x70)=(1u<<2)|(1u<<16);
 prv_enable(bus);assert(prv_start(bus,&xfers[0])==0 && pending && REG(0x44)==0x002c0032);
 pbl_spi_t5ai_irq_handler();assert(dma_buf==pixels && dma_len==65536 && submits==1 && s_timeout.active);
 dma_cb(dma_arg,0);assert(dma_buf==pixels+65536 && dma_len==1864 && submits==2 && s_timeout.active);
 REG(0x70)=(1u<<2)|(1u<<14);drain_steps=3;
 dma_cb(dma_arg,0);assert(drain_steps==0);assert(completions==1 && !result && !s_timeout.active && !(REG(0x60)&(STREAM|(1u<<6))));
 pbl_spi_t5ai_irq_handler();assert(completions==1);
 state.idx=0;prv_enable(bus);assert(prv_start(bus,&xfers[0])==0);pbl_spi_t5ai_irq_handler();
 s_timeout.fn(&s_timeout);assert(completions==2 && result==-ETIMEDOUT && terminated==1);
 dma_cb(dma_arg,0);assert(completions==2);
 state.idx=0;prv_enable(bus);assert(prv_start(bus,&xfers[0])==0);pbl_spi_t5ai_irq_handler();
 dma_cb(dma_arg,-EIO);assert(completions==3 && result==-EIO && !s_timeout.active);
 const uint8_t command[]={2,0,0x51,0,35};const struct pbl_spi_xfer reg={.tx_buf=command,.len=5};
 state=(struct pbl_spi_bus_state){&reg,0,1};prv_enable(bus);assert(prv_start(bus,&reg)==0 && REG(0x44)==0x00510002 && REG(0x100)==35);
 pbl_spi_t5ai_irq_handler();assert(completions==4 && !result);
 REG(0x70)=0;state.idx=0;prv_enable(bus);assert(prv_start(bus,&reg)==0);pbl_spi_t5ai_irq_handler();assert(completions==5 && result==-ETIMEDOUT);
 assert(!depth);return 0;
}
"""


def model(source):
    source = re.sub(r"#define REG\(o\).*", "#define REG(o) regs[(o)/4]", source)
    return re.sub(r"#define SYS\(o\).*", "#define SYS(o) sys[(o)/4]", source)


class SpiTest(unittest.TestCase):
    def test_command_quad_segmentation_error_timeout_and_late_irq(self):
        run("drivers/spi/t5ai.c", PREFIX, MAIN, transform=model)
