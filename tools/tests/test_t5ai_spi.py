# SPDX-License-Identifier: Apache-2.0
"""Run quad SPI command framing, DMA segmentation and timeout callbacks."""

import re
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
 assert(prv_configure(bus,&peripheral)==0 && ((SYSTEM_REG(0x24)>>6)&15)==5);
 peripheral.mode=1;assert(prv_configure(bus,&peripheral)==-ENOTSUP);peripheral.mode=0;
 static uint8_t pixels[65536+1864] __attribute__((aligned(32)));
 const uint8_t header[]={0x32,0,0x2c,0};
 const struct pbl_spi_xfer xfers[]={{.tx_buf=header,.len=4},{.tx_buf=pixels,.len=sizeof pixels,.tx_nbits=4}};
 state=(struct pbl_spi_bus_state){xfers,0,2};QSPI_REG(0x70)=(1u<<2)|(1u<<16);
 prv_enable(bus);assert(prv_start(bus,&xfers[0])==0 && pending && QSPI_REG(0x44)==0x002c0032);
 pbl_spi_t5ai_irq_handler();assert(dma_buf==pixels && dma_len==65536 && submits==1 && s_dma_timeout.active);
 dma_cb(dma_arg,0);assert(dma_buf==pixels+65536 && dma_len==1864 && submits==2 && s_dma_timeout.active);
 QSPI_REG(0x70)=(1u<<2)|(1u<<14);drain_steps=3;
 dma_cb(dma_arg,0);assert(drain_steps==0);assert(completions==1 && !result && !s_dma_timeout.active && !(QSPI_REG(0x60)&(QSPI_STREAM_CONFIG|(1u<<6))));
 pbl_spi_t5ai_irq_handler();assert(completions==1);
 state.idx=0;prv_enable(bus);assert(prv_start(bus,&xfers[0])==0);pbl_spi_t5ai_irq_handler();
 s_dma_timeout.fn(&s_dma_timeout);assert(completions==2 && result==-ETIMEDOUT && terminated==1);
 dma_cb(dma_arg,0);assert(completions==2);
 state.idx=0;prv_enable(bus);assert(prv_start(bus,&xfers[0])==0);pbl_spi_t5ai_irq_handler();
 dma_cb(dma_arg,-EIO);assert(completions==3 && result==-EIO && !s_dma_timeout.active);
 const uint8_t command[]={2,0,0x51,0,35};const struct pbl_spi_xfer reg={.tx_buf=command,.len=5};
 state=(struct pbl_spi_bus_state){&reg,0,1};prv_enable(bus);assert(prv_start(bus,&reg)==0 && QSPI_REG(0x44)==0x00510002 && QSPI_REG(0x100)==35);
 pbl_spi_t5ai_irq_handler();assert(completions==4 && !result);
 QSPI_REG(0x70)=0;state.idx=0;prv_enable(bus);assert(prv_start(bus,&reg)==0);pbl_spi_t5ai_irq_handler();assert(completions==5 && result==-ETIMEDOUT);
 assert(!depth);return 0;
}
"""


def model(source):
    source = re.sub(
        r"#define QSPI_REG\(offset\).*",
        "#define QSPI_REG(offset) regs[(offset)/4]",
        source,
    )
    return re.sub(
        r"#define SYSTEM_REG\(offset\).*",
        "#define SYSTEM_REG(offset) sys[(offset)/4]",
        source,
    )


class SpiTest(unittest.TestCase):
    def test_command_quad_segmentation_error_timeout_and_late_irq(self):
        run("drivers/spi/t5ai.c", PREFIX, MAIN, transform=model)

    def test_actual_mailbox_power_then_clock_failure_short_circuits(self):
        prefix = r"""
#include <assert.h>
typedef int status_t;
#define FAILED(x) ((x)<0)
enum {CP_PM_POWER_COMMAND=1,CP_PM_AHBP_QSPI_POWER=2,CP_PM_POWER_ON=3,
 CP_PM_CLOCK_COMMAND=4,CP_PM_QSPI0_CLOCK=5,CP_PM_CLOCK_ENABLE=6};
static unsigned calls;static int power_result,clock_result;
static int prv_pm_request(unsigned command,unsigned module,unsigned state){
 ++calls;
 if(calls==1){assert(command==1 && module==2 && state==3);return power_result;}
 assert(calls==2 && command==4 && module==5 && state==6);return clock_result;
}
"""
        main = r"""
int main(void){
 power_result=-1;assert(t5ai_cp_mailbox_enable_qspi0()==-1 && calls==1);
 calls=0;power_result=0;clock_result=-2;
 assert(t5ai_cp_mailbox_enable_qspi0()==-2 && calls==2);
 calls=0;clock_result=0;assert(t5ai_cp_mailbox_enable_qspi0()==0 && calls==2);
 return 0;
}
"""
        run(
            "drivers/t5ai/cp_mailbox.c",
            prefix,
            main,
            transform=lambda source: body(source, "t5ai_cp_mailbox_enable_qspi0"),
        )

    def test_acquisition_failures_do_not_touch_controller_or_retry_pm(self):
        prefix = PREFIX.replace(
            "static int t5ai_cp_mailbox_enable_qspi0(void){return 0;}",
            "static unsigned pm_calls,pin_calls,dev_calls,cfg_calls,irq_calls;static int stage;\n"
            "static int t5ai_cp_mailbox_enable_qspi0(void){++pm_calls;return stage<0?stage:0;}",
        )
        prefix = prefix.replace(
            "int state){return 0;}",
            "int state){++pin_calls;return stage==1?-EINVAL:0;}",
        )
        prefix = prefix.replace(
            "const struct pbl_device *d){return 0;}",
            "const struct pbl_device *d){++dev_calls;return stage==2?-ENODEV:0;}",
        )
        prefix = prefix.replace(
            "cfg->dst_addr_width==4);return 0;",
            "cfg->dst_addr_width==4);++cfg_calls;return stage==3?-EIO:0;",
        )
        prefix = prefix.replace(
            "NVIC_EnableIRQ(int irq){assert(irq==20);}",
            "NVIC_EnableIRQ(int irq){assert(irq==20);++irq_calls;}",
        )
        main = r"""
int main(void){
 struct pbl_spi_bus_state state={0};const struct pbl_dma_controller ctrl={0};
 const struct pbl_spi_t5ai dev={.bus={&state},.dma={&ctrl}};
 const int failures[]={-ETIMEDOUT,-EIO,-EBUSY};
 for(unsigned i=0;i<3;++i){
  memset(regs,0xa5,sizeof regs);memset(sys,0x5a,sizeof sys);
  uint32_t r[256],s[128];memcpy(r,regs,sizeof r);memcpy(s,sys,sizeof s);
  stage=failures[i];assert(prv_init(&dev.bus)==-EIO);
  assert(pm_calls==i+1 && !pin_calls && !dev_calls && !cfg_calls && !irq_calls);
  assert(!memcmp(r,regs,sizeof r) && !memcmp(s,sys,sizeof s));
 }
 for(stage=1;stage<=3;++stage){
  memset(regs,0x55,sizeof regs);uint32_t r[256];memcpy(r,regs,sizeof r);
  int expected=stage==1?-EINVAL:stage==2?-ENODEV:-EIO;
  assert(prv_init(&dev.bus)==expected && !irq_calls && !memcmp(r,regs,sizeof r));
  assert(pin_calls==(unsigned)stage && dev_calls==(unsigned)(stage-1) && cfg_calls==(unsigned)(stage==3));
 }
 stage=0;
 for(unsigned gate=0;gate<2;++gate){QSPI_REG(8)=0xa0u|(gate<<1);assert(prv_init(&dev.bus)==0);assert(QSPI_REG(8)==(0xa1u|(gate<<1)));}
 assert(pm_calls==8 && irq_calls==2 && !depth);return 0;
}
"""
        run("drivers/spi/t5ai.c", prefix, main, transform=model)

    def test_submission_refusal_and_wire_drain_timeout_are_distinct(self):
        prefix = PREFIX.replace(
            "static unsigned drain_steps;",
            "static unsigned drain_steps;static int prep_error;",
        )
        prefix = prefix.replace(
            "dma_arg=arg;return 0;", "dma_arg=arg;return prep_error;"
        )
        main = r"""
int main(void){
 struct pbl_spi_bus_state state={0};const struct pbl_dma_controller ctrl={0};
 const struct pbl_spi_t5ai dev={.bus={&state},.dma={&ctrl}};
 assert(prv_init(&dev.bus)==0);
 static uint32_t pixels[8];const struct pbl_spi_xfer xfer={.tx_buf=pixels,.len=sizeof pixels,.tx_nbits=4};
 state=(struct pbl_spi_bus_state){&xfer,0,1};
 prv_enable(&dev.bus);s_quad_stream_active=true;prep_error=-EBUSY;
 assert(prv_start(&dev.bus,&xfer)==-EBUSY && !s_dma_active && !s_dma_timeout.active);
 assert(!completions);prv_disable(&dev.bus);
 prep_error=0;prv_enable(&dev.bus);s_quad_stream_active=true;
 assert(prv_start(&dev.bus,&xfer)==0 && s_dma_timeout.active);
 QSPI_REG(0x70)=1u<<14;dma_cb(dma_arg,0);
 assert(completions==1 && result==-ETIMEDOUT && !s_dma_timeout.active);
 dma_cb(dma_arg,0);assert(completions==1 && !depth);return 0;
}
"""
        run(
            "drivers/spi/t5ai.c",
            prefix,
            main,
            transform=model,
        )
