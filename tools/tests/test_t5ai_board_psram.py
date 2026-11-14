# SPDX-License-Identifier: Apache-2.0
"""Exercise actual board readiness guards before any framebuffer access."""

import unittest

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_native_objects import body


class BoardPsramTest(unittest.TestCase):
    def test_failed_power_or_range_validation_precedes_display_and_sleep(self):
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
typedef int status_t;
#define FAILED(s) ((s)<0)
#define PBL_LOG_ERR(...)
static jmp_buf failure;
#define PBL_ASSERTN(x) do {if(!(x))longjmp(failure,1);}while(0)
static unsigned checks,render,brightness,sleep,stage;
static uint32_t __t5ai_framebuffer_start__[1],__t5ai_framebuffer_end__[1];
static uint32_t __t5ai_display_dma_start__[1],__t5ai_display_dma_end__[1];
static int t5ai_cp_mailbox_vote_app_sleep(bool value){assert(!value);return 0;}
static void t5ai_power_init(void){}
static int t5ai_cp_mailbox_enable_psram(void){return stage==1?-1:0;}
static bool prv_validate_psram_range(uint32_t *a,uint32_t *b){return stage!=++checks+1;}
static void t5ai_boot_splash_render(void){assert(checks==2);++render;}
static void t5ai_sleep_start(void){assert(render && brightness);++sleep;}
#define BACKLIGHT 0
static const struct {unsigned backlight_on_percent;} BOARD_CONFIG={100};
static int pbl_backlight_set_brightness(int device,unsigned value){assert(render);++brightness;return 0;}
"""
        main = r"""
int main(void){
 for(stage=1;stage<=3;++stage){checks=0;
  if(!setjmp(failure)){board_init();assert(false);}
  assert(!render && !brightness && !sleep);
  if(stage==1)assert(!checks);
 }
 stage=0;checks=0;board_init();assert(checks==2 && render==1 && brightness==1 && sleep==1);
 return 0;}
"""
        run(
            "fw/board/boards/board_unhygienix.c",
            prefix,
            main,
            defines=("CONFIG_T5AI_RETAINED_SLEEP",),
            transform=lambda source: body(source, "board_init"),
        )


if __name__ == "__main__":
    unittest.main()
