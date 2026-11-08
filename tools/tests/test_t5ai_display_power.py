# SPDX-License-Identifier: Apache-2.0
"""Exercise opt-in display power failures and full framebuffer restoration."""

import unittest

from tools.tests.native_t5ai_harness import DISPLAY_PREFIX, run
from tools.tests.test_t5ai_panel_init import PREFIX as PANEL_PREFIX

DISPLAY = (
    DISPLAY_PREFIX.replace(
        "static int pbl_panel_prepare(const struct pbl_panel *p){return 0;}",
        """static unsigned prepares,enables,disables,sleepins;static unsigned power_failure;
static int pbl_panel_prepare(const struct pbl_panel *p){assert(co5300_lock.held);++prepares;return power_failure==1?-EIO:0;}
static int pbl_panel_unprepare(const struct pbl_panel *p){assert(co5300_lock.held);++sleepins;return power_failure==4?-EIO:0;}""",
    )
    .replace(
        "static int pbl_panel_enable(const struct pbl_panel *p){return 0;}",
        "static int pbl_panel_enable(const struct pbl_panel *p){assert(co5300_lock.held);++enables;return power_failure==2?-EIO:0;}",
    )
    .replace(
        "static int pbl_panel_disable(const struct pbl_panel *p){return 0;}",
        "static int pbl_panel_disable(const struct pbl_panel *p){assert(co5300_lock.held);++disables;return power_failure==3?-EIO:0;}",
    )
)
MAIN = r"""
int main(void){
 struct pbl_display_state state={0};struct pbl_display disp={&state,260,260};
 struct pbl_display_buffer buf={&pixels[0][0],264};
 prv_init_conversion_tables();
 for(unsigned y=0;y<260;++y)for(unsigned x=0;x<264;++x)pixels[y][x]=3*x+5*y;
 power_failure=3;assert(prv_disable(&disp)==-EIO && s_disabled && s_reprepare);
 assert(!s_sleep_complete && sleepins==0);
 assert(prv_update(&disp,&buf,17,32)==-EHOSTDOWN && !callbacks && !transfers);
 assert(prv_clear(&disp)==-EHOSTDOWN && !transfers);
 power_failure=4;assert(prv_disable(&disp)==-EIO && sleepins==1 && !s_sleep_complete);
 power_failure=0;assert(prv_disable(&disp)==0 && s_sleep_complete && sleepins==2);
 assert(prv_disable(&disp)==0 && sleepins==2); // Applied sleep is idempotent.
 power_failure=1;assert(prv_enable(&disp)==-EIO && s_disabled && s_reprepare);
 power_failure=0;assert(prv_enable(&disp)==0 && !s_disabled && !s_reprepare && s_full_redraw);
 assert(prepares==2 && enables==0 && s_enable_pending);
 power_failure=2;assert(prv_update(&disp,&buf,17,32)==0 && completed_status==-EIO && s_full_redraw);
 assert(s_enable_pending && enables==1);
 power_failure=0;callbacks=transfers=0;
 fail_at=1;assert(prv_update(&disp,&buf,17,32)==0 && completed_status==-EIO && s_full_redraw);
 fail_at=0;callbacks=transfers=0;memset(panel,0x5a,sizeof panel);
 assert(prv_update(&disp,&buf,17,32)==0 && callbacks==1 && !completed_status && !s_full_redraw);
 // A partial framework request after reset must restore every physical pixel.
 for(unsigned y=0;y<466;++y)for(unsigned x=0;x<466;++x){
  unsigned c=pixels[y*260/466][x*260/466]&63;static const unsigned rb[]={0,10,21,31};
  unsigned rgb=(rb[c>>4]<<11)|(((c>>2)&3)*21<<5)|rb[c&3];
  assert(panel[y][2*x]==rgb>>8 && panel[y][2*x+1]==(rgb&255));
 }
 assert(!s_enable_pending && enables==2);
 assert(prv_enable(&disp)==0 && prepares==2); // No repeated reset while active.
 assert(!co5300_lock.held);
 return 0;
}
"""

# Reuse the real prepare framing checks, adding sleep/brightness after init.
PANEL = PANEL_PREFIX.replace(
    "static uint8_t brightness;", "static uint8_t brightness;static bool power_phase;"
)
PANEL = PANEL.replace(
    "static void delay_us(unsigned us){",
    "static void delay_us(unsigned us){if(power_phase){assert(us==200000);++delays;return;}",
)
PANEL = PANEL.replace(
    " static const uint8_t commands[]=",
    """ if(power_phase){const uint8_t *b=x->tx_buf;assert(count==1 && b[0]==2 && !b[1] && !b[3]);
 assert(b[2]==0x10 || (b[2]==0x51 && x->len==5 && b[4]==brightness));
 return ++calls==fail_at?-EIO:0;}
 static const uint8_t commands[]=""",
)
PANEL_MAIN = r"""
int main(void){
 brightness=s_brightness=35;assert(prv_prepare(&CO5300.panel)==0 && s_prepared);
 power_phase=true;calls=delays=0;
 assert(prv_unprepare(&CO5300.panel)==0 && !s_prepared && calls==1 && delays==1);
 brightness=0;assert(pbl_panel_co5300_set_brightness(0)==0 && !s_brightness && calls==1);
 brightness=77;assert(pbl_panel_co5300_set_brightness(77)==0 && s_brightness==77 && calls==1);
 s_prepared=true;fail_at=2;assert(prv_unprepare(&CO5300.panel)==-EIO && !s_prepared && delays==1);
 assert(pbl_panel_co5300_set_brightness(77)==0 && calls==2);
 return 0;
}
"""
BACKLIGHT = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define nullptr NULL
#define T5AI_AMOLED_MIN_BRIGHTNESS 35u
struct pbl_backlight_state {uint8_t brightness;};
struct pbl_backlight {struct pbl_backlight_state *state;int dev;};
struct pbl_backlight_ops {int (*init)(const struct pbl_backlight *);int (*set_brightness)(const struct pbl_backlight *,uint8_t);uint8_t (*get_level)(const struct pbl_backlight *,uint8_t);int (*refresh)(const struct pbl_backlight *);};
static uint8_t raw;
static int pbl_panel_co5300_set_brightness(uint8_t level){raw=level;return 0;}
#define PBL_BACKLIGHT_STATE_DEFINE(n) static struct pbl_backlight_state state
#define PBL_BACKLIGHT_INIT(...) {&state,0}
#define PBL_DEVICE_REGISTER(...)
"""
BACKLIGHT_MAIN = r"""
int main(void){
 assert(prv_init(&pbl_backlight_t5ai)==0);
#ifdef CONFIG_T5AI_AMOLED_ZERO_BRIGHTNESS
 assert(raw==0);
#else
 assert(raw==35);
#endif
 for(unsigned i=1;i<=100;++i){unsigned v=(i*255+99)/100;assert(prv_get_level(NULL,i)==(v<35?35:v));}
 assert(prv_set_brightness(&pbl_backlight_t5ai,100)==0 && raw==255);
 return 0;
}
"""


class DisplayPowerTest(unittest.TestCase):
    def test_serialized_sleep_errors_and_full_redraw(self):
        run(
            "drivers/display/t5ai/display_co5300.c",
            DISPLAY,
            MAIN,
            defines=["CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE"],
        )

    def test_sleep_settle_error_and_cached_brightness(self):
        run(
            "drivers/panel/co5300.c",
            PANEL,
            PANEL_MAIN,
            defines=["CONFIG_T5AI_AMOLED_SLEEP_ON_DISABLE"],
        )

    def test_zero_opt_in_and_unchanged_nonzero_floor(self):
        for enabled in [False, True]:
            run(
                "drivers/backlight/t5ai.c",
                BACKLIGHT,
                BACKLIGHT_MAIN,
                defines=["CONFIG_T5AI_AMOLED_ZERO_BRIGHTNESS"] if enabled else [],
            )
