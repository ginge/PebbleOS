# SPDX-License-Identifier: Apache-2.0
"""Run the native panel sequence and each register-write failure."""

import unittest

from tools.tests.native_t5ai_harness import run

PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define nullptr NULL
#define PBL_FOREVER 0
#define PBL_GPIO_OUTPUT_INACTIVE 1
#define ARRAY_LENGTH(a) (sizeof(a)/sizeof((a)[0]))
struct pbl_device {int unused;};struct pbl_panel {int unused;};struct pbl_gpio {int unused;};
struct pbl_spi_dev {int unused;};struct pbl_mutex {int unused;};
#define PBL_MUTEX_DEFINE(n) struct pbl_mutex n
struct pbl_panel_ops {int (*prepare)(const struct pbl_panel *);int (*enable)(const struct pbl_panel *);int (*disable)(const struct pbl_panel *);int (*unprepare)(const struct pbl_panel *);};
struct pbl_spi_xfer {const void *tx_buf;uint32_t len;};
static const struct {struct pbl_panel panel;struct pbl_gpio reset;struct pbl_spi_dev spi;} CO5300;
static unsigned calls,fail_at,delays,reset_step;
static uint8_t brightness;
static int pbl_gpio_configure(const struct pbl_gpio *p,int flags){return 0;}
static int pbl_gpio_set(const struct pbl_gpio *p,bool high){assert(high==(reset_step!=1));++reset_step;return 0;}
static int pbl_spi_setup(const struct pbl_spi_dev *s){return 0;}
static void pbl_mutex_lock(struct pbl_mutex *m,int timeout){}
static void pbl_mutex_unlock(struct pbl_mutex *m){}
static void delay_us(unsigned us){static const unsigned expected[]={20000,200000,120000,200000};assert(delays<4 && us==expected[delays++]);}
static int pbl_spi_transfer(const struct pbl_spi_dev *d,const struct pbl_spi_xfer *x,size_t count){
 static const uint8_t commands[]={0xfe,0x19,0x1c,0xfe,0xc4,0x3a,0x35,0x53,0x51,0x63,0x11,0x20,0x13,0x29};
 static const uint8_t values[]={0x20,0x10,0xa0,0,0x80,0x55,0,0x20,0,0xff};
 assert(calls<sizeof commands && count==1 && reset_step==3);
 const uint8_t *b=x->tx_buf;assert(b[0]==2 && !b[1] && b[2]==commands[calls] && !b[3]);
 assert(x->len==(calls<10?5u:4u));if(calls<10)assert(b[4]==(calls==8?brightness:values[calls]));
 return ++calls==fail_at ? -EIO : 0;
}
"""
MAIN = r"""
int main(void){for(unsigned level=35;level<=255;level+=110){brightness=level;s_brightness=level;
 for(fail_at=0;fail_at<=14;++fail_at){calls=delays=reset_step=0;s_prepared=false;
 assert(pbl_panel_co5300_init(NULL)==0);
 int ret=prv_prepare(&CO5300.panel);if(ret==0)ret=prv_enable(&CO5300.panel);
 assert((ret==0)==(fail_at==0));assert(calls==(fail_at?fail_at:14));}}
 return 0;}
"""


class PanelInitTest(unittest.TestCase):
    def test_reset_command_order_and_every_spi_error(self):
        run("drivers/panel/co5300.c", PREFIX, MAIN)
