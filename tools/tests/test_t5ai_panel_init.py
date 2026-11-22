# SPDX-License-Identifier: Apache-2.0
"""Run the native panel sequence and each register-write failure."""

import unittest

from tools.tests.t5ai_panel_harness import run_panel

PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <pbl/drivers/panel/co5300.h>
#include <pbl/soc/t5ai/delay.h>
static struct pbl_panel_co5300_state panel_state = {.lock=PBL_MUTEX_INITIALIZER};
const struct pbl_panel_co5300 CO5300 = {.state=&panel_state};
static unsigned calls,fail_at,delays,reset_step;
static uint8_t brightness;
int pbl_gpio_configure(const struct pbl_gpio *p,uint32_t flags){return 0;}
int pbl_gpio_set(const struct pbl_gpio *p,bool high){assert(high==(reset_step!=1));++reset_step;return 0;}
int pbl_spi_setup(const struct pbl_spi_dev *s){return 0;}
int pbl_mutex_lock_lr(struct pbl_mutex *m,pbl_timeout_t timeout,uintptr_t lr){assert(!m->count);++m->count;return 0;}
void pbl_mutex_unlock(struct pbl_mutex *m){assert(m->count==1);--m->count;}
void delay_us(uint32_t us){static const unsigned expected[]={20000,200000,120000,200000};assert(delays<4 && us==expected[delays++]);}
int pbl_spi_transfer(const struct pbl_spi_dev *d,const struct pbl_spi_xfer *x,size_t count){
 static const uint8_t commands[]={0xfe,0x19,0x1c,0xfe,0xc4,0x3a,0x35,0x53,0x51,0x63,0x11,0x20,0x13,0x29};
 static const uint8_t values[]={0x20,0x10,0xa0,0,0x80,0x55,0,0x20,0,0xff};
 assert(calls<sizeof commands && count==1 && reset_step==3);
 const uint8_t *b=x->tx_buf;assert(b[0]==2 && !b[1] && b[2]==commands[calls] && !b[3]);
 assert(x->len==(calls<10?5u:4u));if(calls<10)assert(b[4]==(calls==8?brightness:values[calls]));
 return ++calls==fail_at ? -EIO : 0;
}
"""
MAIN = r"""
int main(void){for(unsigned level=35;level<=255;level+=110){brightness=level;panel_state.brightness=level;
 for(fail_at=0;fail_at<=14;++fail_at){calls=delays=reset_step=0;panel_state.prepared=false;
 assert(pbl_panel_co5300_init(&CO5300.panel.dev)==0);
 int ret=prv_prepare(&CO5300.panel);if(ret==0)ret=prv_enable(&CO5300.panel);
 assert((ret==0)==(fail_at==0));assert(calls==(fail_at?fail_at:14));}}
 return 0;}
"""


class PanelInitTest(unittest.TestCase):
    def test_reset_command_order_and_every_spi_error(self):
        run_panel(PREFIX, MAIN)
