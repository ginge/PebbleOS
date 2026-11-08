# SPDX-License-Identifier: Apache-2.0
"""Preserve pointed-to CP payloads across FIFO and AP snapshot lifetimes."""

import hashlib
import json
import unittest
from pathlib import Path

from tools.tests.native_t5ai_harness import run
from tools.tests.test_t5ai_native_objects import body

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int bk_err_t;
typedef uintptr_t u32;
typedef unsigned mailbox_endpoint_t;
typedef struct {uint32_t param0,param1,param2,param3;} mailbox_data_t;
typedef struct {uintptr_t data[2];uint8_t dest_cpu;} mbox0_message_t;
#define MBOX_CHNL_NUM 3
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_MAILBOX_SRC_DST -2
#define BK_ERR_MAILBOX_TIMEOUT -3
#define BK_ERR_MAILBOX_NOT_INIT -4
#define MBOX0_HAL_OK 0
#define RX_FIFO_STAT_FULL 1u

static uint8_t mailbox_tx_box[3];
static mbox0_message_t fifo[3];static unsigned count;
static int mbox0_drv_get_send_stat(unsigned dst,uint32_t *status){assert(dst==1);*status=count==3?1:0;return 0;}
static int mbox0_drv_send_message(mbox0_message_t *message){assert(count<3);fifo[count++]=*message;return 0;}
"""
MAIN = r"""
int main(void){
 for(unsigned id=1;id<=3;++id){mailbox_data_t value={id,0,0,0};assert(!bk_mailbox_send(&value,0,1,0));}

 const uint8_t before=mailbox_tx_box[1];mailbox_data_t full={999,0,0,0};
 assert(bk_mailbox_send(&full,0,1,0)==BK_ERR_MAILBOX_TIMEOUT && mailbox_tx_box[1]==before);
 for(unsigned id=1;id<=100;++id){
  mailbox_data_t *inflight=(mailbox_data_t *)fifo[0].data[0];
  memmove(fifo,fifo+1,2*sizeof(*fifo));--count;
  mailbox_data_t next={id+3,0,0,0};assert(!bk_mailbox_send(&next,0,1,0));
  assert(inflight->param0==id); // pop does not grant reuse until the AP copies it
  for(unsigned i=0;i<3;++i)assert(((mailbox_data_t *)fifo[i].data[0])->param0==id+i+1);
 }
 return 0;
}
"""


class CPMailboxLifetimeTest(unittest.TestCase):
    def test_actual_adapter_preserves_queued_and_popped_payloads(self):
        overlay = json.loads(
            (ROOT / "boards/unhygienix/tuya/cp/variants/sleep-overlay.json").read_text()
        )
        recipe = next(
            item for item in overlay if item["path"].endswith("/mbox0_adapter.c")
        )
        path = ROOT / "third_party/hal_tuya/TuyaOpen-T5AI" / recipe["path"]
        self.assertEqual(
            hashlib.sha256(path.read_bytes()).hexdigest(), recipe["vendor_sha256"]
        )
        source = path.read_text()
        for replacement in recipe["replacements"]:
            self.assertEqual(source.count(replacement["old"]), replacement["count"])
            source = source.replace(replacement["old"], replacement["new"])
        declarations = next(
            item["new"]
            for item in recipe["replacements"]
            if "#define NATIVE_MAILBOX_MAX_RX_FIFO_DEPTH" in item["new"]
        )
        run(
            "boards/unhygienix/tuya/cp/native_alarm.c",
            FIXTURE + declarations,
            MAIN,
            transform=lambda _: body(source, "bk_mailbox_send"),
        )
