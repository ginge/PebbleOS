# SPDX-License-Identifier: Apache-2.0
"""Compile the actual panel driver to exercise applied-state caching and faults."""

import unittest

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_display_power import DISPLAY
from tools.tests.test_t5ai_display_power import MAIN as DISPLAY_MAIN
from tools.tests.test_t5ai_panel_init import PREFIX

PREFIX = PREFIX[: PREFIX.index("static int pbl_gpio_set")] + r"""
static unsigned gpio_fail;
static int pbl_gpio_set(const struct pbl_gpio *p,bool high){return ++reset_step==gpio_fail?-EIO:0;}
static int pbl_spi_setup(const struct pbl_spi_dev *s){return 0;}
static void pbl_mutex_lock(struct pbl_mutex *m,int timeout){}
static void pbl_mutex_unlock(struct pbl_mutex *m){}
static void delay_us(unsigned us){++delays;}
static unsigned last_command,last_value;
static int pbl_spi_transfer(const struct pbl_spi_dev *d,const struct pbl_spi_xfer *x,size_t count){
 const uint8_t *b=x->tx_buf;assert(count==1 && b[0]==2 && !b[1] && !b[3]);
 last_command=b[2];last_value=x->len==5?b[4]:0;
 return ++calls==fail_at?-EIO:0;
}
"""
MAIN = r"""
int main(void){
 // Desired brightness changes while unprepared do no I/O.
 assert(pbl_panel_co5300_set_brightness(35)==0 && calls==0);
 assert(prv_prepare(&CO5300.panel)==0 && s_prepared && s_brightness_valid);
 unsigned n=calls;
 for(unsigned i=0;i<100;++i)assert(pbl_panel_co5300_set_brightness(35)==0);
 assert(calls==n);
 assert(pbl_panel_co5300_set_brightness(36)==0 && calls==++n && last_command==0x51 && last_value==36);
 fail_at=calls+1;
 assert(pbl_panel_co5300_set_brightness(77)==-EIO && !s_brightness_valid && s_brightness==77);
 fail_at=0;
 assert(pbl_panel_co5300_set_brightness(77)==0 && calls==n+2 && s_brightness_valid);
 n=calls;
 // Failed commands never become applied state, even if the old state matches a later request.
 assert(prv_enable(&CO5300.panel)==0 && calls==++n);
 assert(prv_enable(&CO5300.panel)==0 && calls==n);
 fail_at=calls+1;assert(prv_disable(&CO5300.panel)==-EIO && calls==++n);
 fail_at=0;assert(prv_enable(&CO5300.panel)==0 && calls==++n);
 assert(prv_disable(&CO5300.panel)==0 && calls==++n);
 assert(prv_disable(&CO5300.panel)==0 && calls==n);
 fail_at=calls+1;assert(prv_enable(&CO5300.panel)==-EIO && calls==++n);
 fail_at=0;assert(prv_enable(&CO5300.panel)==0 && calls==++n);
 // Failed sleep invalidates both caches; no brightness work until prepare.
 fail_at=calls+1;assert(prv_unprepare(&CO5300.panel)==-EIO && !s_prepared && !s_brightness_valid);
 n=calls;assert(pbl_panel_co5300_set_brightness(88)==0 && calls==n);
 fail_at=0;assert(prv_prepare(&CO5300.panel)==0);
 n=calls;assert(prv_enable(&CO5300.panel)==0 && calls==++n);
 assert(pbl_panel_co5300_set_brightness(88)==0 && calls==n);
 // Every prepare failure must discard previously valid caches, in both configurations.
 for(unsigned step=1;step<=13;++step){
  fail_at=0;assert(prv_prepare(&CO5300.panel)==0);
  fail_at=calls+step;assert(prv_prepare(&CO5300.panel)==-EIO && !s_prepared && !s_brightness_valid);
  n=calls;assert(pbl_panel_co5300_set_brightness(99)==0 && calls==n);
 }
 fail_at=0;
 for(unsigned step=1;step<=3;++step){
  gpio_fail=0;assert(prv_prepare(&CO5300.panel)==0);
  gpio_fail=reset_step+step;n=calls;
  assert(prv_prepare(&CO5300.panel)==-EIO && !s_prepared && !s_brightness_valid && calls==n);
 }
 return 0;
}
"""


class PanelPowerCacheTest(unittest.TestCase):
    def test_applied_state_faults_and_reset_invalidation(self):
        for defines in [[], ["CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE"]]:
            with self.subTest(defines=defines):
                run("drivers/panel/co5300.c", PREFIX, MAIN, defines=defines)

    def test_repeated_partial_wake_and_orientation(self):
        cycles = r"""
 for(unsigned cycle=0;cycle<32;++cycle){
  state.rotated=(cycle&1)!=0;
  assert(prv_disable(&disp)==0 && s_disabled);
  assert(prv_disable(&disp)==0);
  assert(prv_enable(&disp)==0 && s_full_redraw && s_enable_pending);
  assert(prv_enable(&disp)==0 && s_full_redraw && s_enable_pending);
  callbacks=transfers=0;fail_at=3;
  assert(prv_update(&disp,&buf,31,31)==0 && completed_status==-EIO && s_full_redraw);
  fail_at=0;callbacks=transfers=0;memset(panel,0x5a,sizeof panel);
  assert(prv_update(&disp,&buf,31,31)==0 && callbacks==1 && !completed_status);
  assert(!s_full_redraw && !s_enable_pending);
  for(unsigned y=0;y<466;++y)for(unsigned x=0;x<466;++x){
   unsigned sy=y*260/466,sx=x*260/466;
   if(state.rotated){sy=259-sy;sx=259-sx;}
   unsigned c=pixels[sy][sx]&63;static const unsigned rb[]={0,10,21,31};
   unsigned rgb=(rb[c>>4]<<11)|(((c>>2)&3)*21<<5)|rb[c&3];
   assert(panel[y][2*x]==rgb>>8 && panel[y][2*x+1]==(rgb&255));
  }
 }
"""
        run(
            "drivers/display/t5ai/display_co5300.c",
            DISPLAY,
            DISPLAY_MAIN.replace(" return 0;", cycles + " return 0;"),
            defines=["CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE"],
        )
