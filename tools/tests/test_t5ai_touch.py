# SPDX-License-Identifier: Apache-2.0
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class T5AITouchTest(unittest.TestCase):
    def test_backpressure_and_release(self):
        source = (ROOT / "drivers/touch/cst92xx/cst92xx_t5ai.c").read_text()
        body = source[
            source.index("static bool prv_delivery_ready") : source.index(
                "static void prv_interrupt_handler"
            )
        ]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef unsigned pbl_tick_t;
struct pbl_msgq { unsigned used; };
#define pbl_irq_lock() ((void)0)
#define pbl_irq_unlock() ((void)0)
#define PebbleTask_App 0
#define PebbleTask_Worker 1
#define PebbleTask_KernelBackground 2
#define PBL_DISPLAY_WIDTH 260
#define PBL_DISPLAY_HEIGHT 260
#define pbl_ms_to_ticks(x) (x)
#define PBL_LOG_ERR(...) ((void)0)
#define PBL_LOG_WRN(...) ((void)0)
#define TouchState_FingerDown 1
#define TouchState_FingerUp 0
static bool s_enabled=true,s_finger_down,s_contact_active,s_release_pending;
static int16_t s_last_x,s_last_y;
static pbl_tick_t s_last_motion_ticks;
static unsigned s_read_failures,s_consecutive_read_failures;
static struct pbl_msgq queues[3];
static unsigned ticks,count;
static unsigned report_x=100;
static bool contact=true;
static int states[20],xs[20];
static struct pbl_msgq * pebble_task_get_to_queue(unsigned task){return &queues[task];}
static struct pbl_msgq * event_get_to_kernel_queue(unsigned task){return &queues[task];}
static unsigned pbl_msgq_num_used(struct pbl_msgq * q){return q->used;}
static unsigned pbl_uptime_ticks(void){return ticks;}
static void prv_reset(bool run){(void)run;}
static int16_t prv_map_coordinate(unsigned c,int16_t size){return c*size/466;}
static bool prv_read_report(uint8_t *r){
 r[0]=6;r[5]=contact;r[6]=0xab;
 r[1]=report_x>>4;r[3]=(report_x&15)<<4;
 return true;
}
static void touch_handle_update(int state,int x,int y){
 (void)y;assert(count<20);states[count]=state;xs[count++]=x;
}
"""
        # Pull constants from the driver while retaining literal report bytes below.
        constants = source[
            source.index("#define CST92XX_RESET_ASSERT_MS") : source.index(
                "static volatile bool s_enabled"
            )
        ]
        harness += constants
        checks = r"""
int main(void){
 assert(prv_poll()); assert(count==1 && states[0]==1);
 queues[0].used=2;ticks=100;report_x=200;
 assert(prv_poll());assert(count==1 && s_last_x==148);
 queues[0].used=0;queues[2].used=2;ticks=200;
 assert(prv_poll());assert(count==1);
 queues[2].used=0;ticks=300;report_x=300;
 assert(prv_poll());assert(count==2 && xs[1]==92);
 queues[0].used=2;contact=false;
 assert(prv_poll());assert(count==2 && s_release_pending);
 assert(prv_poll());assert(count==2);
 queues[0].used=0;
 assert(prv_poll());assert(count==4 && states[3]==0 && xs[3]==92);
 assert(!s_finger_down && !s_release_pending);
 contact=true;queues[1].used=2;
 assert(prv_poll());assert(count==4 && s_contact_active && !s_finger_down);
 queues[1].used=0;
 assert(prv_poll());assert(count==5 && s_finger_down);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "test.c").write_text(harness + body + checks)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)

    def test_retry_after_reset_without_interrupt(self):
        source = (ROOT / "drivers/touch/cst92xx/cst92xx_t5ai.c").read_text()
        task = source[
            source.index("static void prv_touch_task") : source.index(
                "void touch_sensor_init"
            )
        ]
        constants = source[
            source.index("#define CST92XX_RESET_ASSERT_MS") : source.index(
                "static volatile bool s_enabled"
            )
        ]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#define PBL_TICK_FOREVER 0xffffffffu
#define pbl_ms_to_ticks(x) (x)
static bool s_enabled=true,s_finger_down,s_contact_active;
static unsigned s_consecutive_read_failures,s_touch_wake,polls,waits;
static int s_interrupt_input;
static jmp_buf done;
static unsigned delays[5];
static bool prv_poll(void) {
 ++polls;
 s_consecutive_read_failures=polls==3 ? 0 : polls;
 return polls==4;
}
static int pbl_gpio_get_raw(const int *p){(void)p;return 1;}
typedef struct {unsigned ticks;} pbl_timeout_t;
#define PBL_TICKS(t) ((pbl_timeout_t){t})
static int pbl_sem_take(unsigned *semaphore,pbl_timeout_t timeout) {
 (void)semaphore;
 delays[waits++]=timeout.ticks;
 if(waits==5)longjmp(done,1);
 return 0;
}
"""
        checks = r"""
int main(void) {
 if(!setjmp(done))prv_touch_task(0);
 assert(polls==4 && waits==5);
 assert(delays[0]==PBL_TICK_FOREVER && delays[1]==80 && delays[2]==80);
 assert(delays[3]==1000 && delays[4]==PBL_TICK_FOREVER);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "test.c").write_text(harness + constants + task + checks)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)
