# SPDX-License-Identifier: Apache-2.0
"""Verify the CP flash transition does not read XIP data while disconnected."""

import hashlib
import json
import re
import unittest

from tools.tests.native_t5ai_harness import ROOT, run
from tools.tests.test_t5ai_native_objects import body


class CPFlashResume(unittest.TestCase):
    def test_quad_parameters_are_loaded_before_flash_disconnect(self):
        recipe = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        entry = next(
            item for item in recipe if item["path"].endswith("/flash_driver.c")
        )
        path = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / entry["path"]
        if not path.is_file():
            self.skipTest("Pinned T5AI source is required")
        data = path.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), entry["vendor_sha256"])
        source = data.decode()
        for replacement in entry["replacements"]:
            self.assertEqual(source.count(replacement["old"]), replacement["count"])
            source = source.replace(replacement["old"], replacement["new"])
        source = re.sub(r'__attribute__\(\(section\("[^\"]+"\)\)\)', "", source)
        function = body(source, "flash_set_line_mode")
        prefix = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#define CONFIG_FLASH_QUAD_ENABLE 1
#define CONFIG_SOC_BK7236XX 1
#define FLASH_LINE_MODE_TWO 2u
#define FLASH_LINE_MODE_FOUR 4u
#define FLASH_MODE_DUAL 1u
#define FLASH_MODE_QUAD 2u
typedef uint32_t flash_line_mode_t;
typedef struct {uint32_t line_mode,coutinuous_read_mode_bits_val;} config_t;
static struct {unsigned hal;flash_line_mode_t flash_line_mode;config_t *flash_cfg;} s_flash;
static size_t page_size;
static unsigned critical,wires=1,clears,quad_writes,qe_calls,hw_mode;
static uint32_t flash_enter_critical(void){assert(!critical);critical=1;return 17;}
static void flash_exit_critical(uint32_t saved){assert(critical && wires && saved==17);critical=0;}
static void flash_set_qe(void){assert(critical && wires);++qe_calls;}
static void flash_hal_clear_qwfr(unsigned *hw){assert(critical && wires);++clears;}
static void sys_drv_set_sys2flsh_2wire(uint32_t enabled){
 assert(critical && enabled!=wires);
 assert(!mprotect(s_flash.flash_cfg,page_size,enabled?PROT_READ|PROT_WRITE:PROT_NONE));
 wires=enabled;
}
static void flash_hal_set_quad_m_value(unsigned *hw,uint32_t value){
 assert(critical && !wires && value==0xa5);++quad_writes;
}
static void flash_hal_set_mode(unsigned *hw,uint32_t mode){assert(critical && !wires);hw_mode=mode;}
"""
        main = r"""
int main(void){
 page_size=(size_t)sysconf(_SC_PAGESIZE);
 s_flash.flash_cfg=mmap(NULL,page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(s_flash.flash_cfg!=MAP_FAILED);
 s_flash.flash_cfg->line_mode=4;s_flash.flash_cfg->coutinuous_read_mode_bits_val=0xa5;
 s_flash.flash_line_mode=2;
 assert(flash_set_line_mode(4)==2 && s_flash.flash_line_mode==4 && hw_mode==FLASH_MODE_QUAD);
 assert(qe_calls==1 && quad_writes==1 && clears==1 && wires && !critical);
 assert(flash_set_line_mode(2)==4 && s_flash.flash_line_mode==2 && hw_mode==FLASH_MODE_DUAL);
 assert(qe_calls==1 && clears==2 && wires && !critical);
 assert(flash_set_line_mode(2)==2 && clears==2 && !critical);
 assert(!munmap(s_flash.flash_cfg,page_size));return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix,
            main,
            transform=lambda _: function,
        )

    def test_native_resume_uses_retained_parameters_until_xip_probe(self):
        recipe = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        entry = next(
            item for item in recipe if item["path"].endswith("/flash_driver.c")
        )
        path = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / entry["path"]
        source = path.read_text()
        for replacement in entry["replacements"]:
            self.assertEqual(source.count(replacement["old"]), replacement["count"])
            source = source.replace(replacement["old"], replacement["new"])
        source = re.sub(r'__attribute__\(\(section\("[^\"]+"\)\)\)', "", source)
        retained = re.search(
            r"static struct \{.*?\} s_native_flash_resume;", source, re.DOTALL
        ).group()
        functions = "\n".join(
            body(source, name)
            for name in (
                "unhygienix_native_flash_prepare",
                "unhygienix_native_flash_prepare_mask_only",
                "unhygienix_native_flash_ready",
                "unhygienix_native_flash_resume",
            )
        )
        prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#define CONFIG_SOC_BK7236XX 1
#define FLASH_LINE_MODE_TWO 2u
#define FLASH_LINE_MODE_FOUR 4u
#define FLASH_MODE_DUAL 1u
#define FLASH_MODE_QUAD 2u
typedef uint32_t flash_line_mode_t;
typedef struct {uint32_t flash_id,line_mode,coutinuous_read_mode_bits_val;} config_t;
static struct {unsigned hal;flash_line_mode_t flash_line_mode;config_t *flash_cfg;} s_flash;
static bool unhygienix_native_sleep_prepared,irq_locked=true,tamper;
static bool mask_only;
static bool unhygienix_native_sleep_mask_only(void){return mask_only;}
static size_t page_size;
static unsigned critical,wires=1,clears,quad_writes,qe_calls,hw_mode,barriers;
static uint32_t flash_enter_critical(void){assert(!critical);critical=1;return 17;}
static void flash_exit_critical(uint32_t saved){assert(critical && wires && saved==17);critical=0;}
static void flash_set_qe(void){assert(critical && wires);++qe_calls;}
static bool unhygienix_native_flash_irqs_locked(void){return irq_locked;}
static void flash_hal_clear_qwfr(unsigned *hw){assert(!critical && irq_locked && wires);++clears;}
static void sys_drv_set_sys2flsh_2wire(uint32_t enabled){
 assert(irq_locked && enabled!=wires);wires=enabled;
 if(enabled){assert(!mprotect(s_flash.flash_cfg,page_size,PROT_READ|PROT_WRITE));
  if(tamper)s_flash.flash_cfg->flash_id^=1;}
}
static void flash_hal_set_quad_m_value(unsigned *hw,uint32_t value){
 assert(irq_locked && !wires && value==0xa5);++quad_writes;
}
static void flash_hal_set_mode(unsigned *hw,uint32_t mode){assert(irq_locked && !wires);hw_mode=mode;}
static void unhygienix_native_flash_barrier(void){assert(wires && irq_locked);++barriers;}
"""
        main = r"""
int main(void){
 page_size=(size_t)sysconf(_SC_PAGESIZE);
 s_flash.flash_cfg=mmap(NULL,page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(s_flash.flash_cfg!=MAP_FAILED);s_flash.flash_cfg->flash_id=0xc86517;
 s_flash.flash_cfg->line_mode=4;s_flash.flash_cfg->coutinuous_read_mode_bits_val=0xa5;
 unhygienix_native_flash_prepare();assert(!unhygienix_native_flash_ready());
 assert(!unhygienix_native_flash_resume() && !clears);
 unhygienix_native_sleep_prepared=true;
 for(unsigned mode=FLASH_LINE_MODE_TWO;mode<=FLASH_LINE_MODE_FOUR;mode+=2){
  s_flash.flash_cfg->line_mode=mode;unhygienix_native_flash_prepare();
  assert(unhygienix_native_flash_ready());irq_locked=false;
  assert(!unhygienix_native_flash_resume());irq_locked=true;
  assert(!mprotect(s_flash.flash_cfg,page_size,PROT_NONE));
  assert(unhygienix_native_flash_resume() && !unhygienix_native_flash_ready());
  assert(s_flash.flash_line_mode==mode && hw_mode==(mode==4?FLASH_MODE_QUAD:FLASH_MODE_DUAL));
 }
 assert(qe_calls==1 && quad_writes==1 && clears==2 && barriers==2 && !critical);
 unhygienix_native_flash_prepare();tamper=true;
 assert(!mprotect(s_flash.flash_cfg,page_size,PROT_NONE));
 assert(!unhygienix_native_flash_resume() && s_native_flash_resume.valid);
 s_flash.flash_cfg->line_mode=0;unhygienix_native_flash_prepare();
 assert(!unhygienix_native_flash_ready() && !unhygienix_native_flash_resume());
 const unsigned qe_before=qe_calls,clears_before=clears,quad_before=quad_writes;
 const unsigned barriers_before=barriers;
 tamper=false;mask_only=true;s_flash.flash_cfg->line_mode=4;
 unhygienix_native_flash_prepare_mask_only();assert(unhygienix_native_flash_ready());
 assert(qe_calls==qe_before && clears==clears_before && quad_writes==quad_before && wires);
 irq_locked=false;assert(!unhygienix_native_flash_resume());irq_locked=true;
 s_flash.flash_cfg->flash_id^=1;assert(!unhygienix_native_flash_resume());
 assert(s_native_flash_resume.valid && qe_calls==qe_before && clears==clears_before);
 s_flash.flash_cfg->flash_id^=1;assert(unhygienix_native_flash_resume());
 assert(!s_native_flash_resume.valid && barriers==barriers_before+2);
 assert(qe_calls==qe_before && clears==clears_before && quad_writes==quad_before && wires);
 assert(!munmap(s_flash.flash_cfg,page_size));return 0;
}
"""
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            prefix + retained,
            main,
            transform=lambda _: functions,
        )


if __name__ == "__main__":
    unittest.main()
