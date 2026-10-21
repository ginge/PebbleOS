# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise CST92xx's full driver with modeled IRQs, timers and KernelBG work."""

import re
import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "drivers/touch/cst92xx/cst92xx_t5ai.c"

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define nullptr NULL
#define CONFIG_DRIVER_TOUCH_LOG_LEVEL 0
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_ERR(...) ((void)0)
#define PBL_LOG_WRN(...) ((void)0)
#define PBL_ASSERTN(x) assert(x)
#define PBL_DISPLAY_WIDTH 260
#define PBL_DISPLAY_HEIGHT 260
#define PBL_FOREVER 0xffffffffu
#define PBL_GPIO_OUTPUT_INACTIVE 1
#define PBL_GPIO_INPUT 2
#define PBL_IRQ_TYPE_EDGE_FALLING 3
#define PBL_INPUT_BTN_TOUCH 4
#define PBL_INPUT_ABS_X 0
#define PBL_INPUT_ABS_Y 1
#define TIMER_INVALID_ID 0
#define PBL_MUTEX_DEFINE(name) struct pbl_mutex name
#define PBL_GPIO(dev,p,f) {.pin=(p)}
#define PBL_IRQ_SPEC(dev,p,f) {.pin=(p)}
#define I2C_CST92XX ((void *)1)
typedef uint32_t TimerID;
typedef struct {void (*cb)(void *);bool scheduled;} RegularTimerInfo;
struct pbl_gpio {unsigned pin;};
struct pbl_irq_spec {unsigned pin;};
struct pbl_mutex {bool held;};
enum {USER,BG,TIMER,ISR};
unsigned context,depth,queue_limit=8,qcount,refs,reads,writes,uses,releases;
unsigned sleeps,resets,timer_creates,timer_ms,samples,enqueue_calls;
bool fail_read,fail_write,irq_enabled,reset_pin,timer_pending,interrupt_during_read;
int pad=1;
uint8_t wire[10];
struct {void (*cb)(void *);void *data;} queue[16];
void (*irq_cb)(void *);
void (*timer_cb)(void *);
void *timer_data;
struct Sample {bool down;int x,y;} output[64],pending;
unsigned input_phase;
void pbl_irq_lock(void){++depth;}
void pbl_irq_unlock(void){assert(depth);--depth;}
void pbl_mutex_lock(struct pbl_mutex *m,unsigned t){
 assert(context!=TIMER && context!=ISR && !depth && !m->held && t==PBL_FOREVER);
 m->held=true;
}
void pbl_mutex_unlock(struct pbl_mutex *m){assert(m->held && !depth);m->held=false;}
void pbl_sleep_ms(unsigned ms){
 assert(context!=TIMER && context!=ISR && !depth && (ms==5 || ms==50));++sleeps;
}
int pbl_gpio_set(const struct pbl_gpio *p,bool value){
 assert(p->pin==43 && context!=TIMER && context!=ISR && !depth);
 reset_pin=value;if(value)++resets;return 0;
}
int pbl_gpio_configure(const struct pbl_gpio *p,unsigned f){
 assert((p->pin==43 && f==1) || (p->pin==42 && f==2));return 0;
}
int pbl_gpio_get_raw(const struct pbl_gpio *p){assert(p->pin==42);return pad;}
int pbl_intc_disable(const struct pbl_irq_spec *p){assert(p->pin==42);irq_enabled=false;return 0;}
int pbl_intc_enable(const struct pbl_irq_spec *p){assert(p->pin==42);irq_enabled=true;return 0;}
int pbl_intc_request(const struct pbl_irq_spec *p,void (*cb)(void *),void *arg){
 assert(p->pin==42 && !arg);irq_cb=cb;return 0;
}
void pbl_i2c_use(const void *p){assert(p==I2C_CST92XX && context!=TIMER && context!=ISR && !depth);++uses;}
void pbl_i2c_release(const void *p){assert(p==I2C_CST92XX);++releases;}
int pbl_i2c_write_block(const void *p,unsigned n,const uint8_t *data){
 assert(p==I2C_CST92XX && n==2 && data[0]==0xd0 && data[1]==0);
 ++writes;return fail_write?-1:0;
}
void fire_irq(void){unsigned saved=context;context=ISR;irq_cb(NULL);context=saved;}
int pbl_i2c_read_block(const void *p,unsigned n,uint8_t *data){
 assert(p==I2C_CST92XX && n==10);++reads;
 if(interrupt_during_read){fire_irq();fire_irq();interrupt_during_read=false;}
 memcpy(data,wire,10);return fail_read?-1:0;
}
TimerID new_timer_create(void){assert(context==USER);return ++timer_creates;}
bool new_timer_start(TimerID id,unsigned ms,void (*cb)(void *),void *data,unsigned flags){
 assert(context==BG && !depth && id==1 && !flags);
 timer_ms=ms;timer_cb=cb;timer_data=data;timer_pending=true;return true;
}
bool new_timer_stop(TimerID id){assert(context!=ISR && !depth && id==1);timer_pending=false;return true;}
bool regular_timer_is_scheduled(RegularTimerInfo *t){return t->scheduled;}
void regular_timer_add_seconds_callback(RegularTimerInfo *t){assert(!t->scheduled);t->scheduled=true;}
bool regular_timer_remove_callback(RegularTimerInfo *t){t->scheduled=false;return true;}
bool system_task_add_callback_from_isr_droppable_raised(void (*cb)(void *),void *data){
 assert(depth);++enqueue_calls;
 if(qcount>=queue_limit)return false;
 queue[qcount].cb=cb;queue[qcount++].data=data;++refs;return true;
}
void pbl_input_report_key(unsigned code,bool down,bool sync){
 assert(context==BG && !depth && code==4 && !sync && !input_phase);
 pending.down=down;input_phase=1;
}
void pbl_input_report_abs(unsigned code,int value,bool sync){
 assert(context==BG && !depth);
 if(code==0){assert(input_phase==1 && !sync);pending.x=value;input_phase=2;}
 else {assert(code==1 && input_phase==2 && sync && samples<64);pending.y=value;output[samples++]=pending;input_phase=0;}
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include DRIVER_SOURCE
#pragma GCC diagnostic pop
void run_one(void){
 assert(qcount && context==USER);
 void (*cb)(void *)=queue[0].cb;void *data=queue[0].data;
 memmove(queue,queue+1,--qcount*sizeof(queue[0]));
 context=BG;cb(data);assert(!s_i2c_lock.held && !depth);context=USER;
 assert(refs);--refs;assert(refs==qcount);
}
void fire_timer(void){
 assert(timer_pending);timer_pending=false;
 context=TIMER;timer_cb(timer_data);context=USER;
}
void fire_watchdog(void){
 assert(s_watchdog_timer.scheduled);context=TIMER;s_watchdog_timer.cb(NULL);context=USER;
}
void report(bool down,unsigned x,unsigned y){
 memset(wire,0,10);wire[0]=6;wire[5]=down;wire[6]=0xab;
 wire[1]=x>>4;wire[2]=y>>4;wire[3]=(x&15)<<4 | (y&15);
}
void setup(void){
 report(false,0,0);touch_sensor_init();touch_sensor_init();
 assert(timer_creates==1 && reads==1 && !reset_pin && !irq_enabled);
 touch_sensor_set_enabled(true);
 assert(irq_enabled && reset_pin && qcount==1 && refs==1 && s_watchdog_timer.scheduled);
}
void check_input_lock(void){
 // Listener-side service locks must never be entered holding the driver's lock.
 assert(!s_i2c_lock.held);
}
"""


class T5AITouchTest(unittest.TestCase):
    def run_driver(self, checks):
        headers = {
            name: ""
            for name in re.findall(r'#include [<"]([^>"\n]+)[>"]', DRIVER.read_text())
            if "/" in name
        }
        headers["pbl/soc/t5ai/draw_profile.h"] = (
            ROOT / "include/pbl/soc/t5ai/draw_profile.h"
        ).read_text()
        source = HARNESS.replace("DRIVER_SOURCE", '"' + str(DRIVER) + '"')
        # Assert lock ordering at the synchronous input boundary.
        source = source.replace(
            "void pbl_input_report_key(unsigned code,bool down,bool sync){",
            "void check_input_lock(void);\nvoid pbl_input_report_key(unsigned code,bool down,bool sync){check_input_lock();",
        )
        run_c(
            source + "\nint main(void){setup();\n" + checks + "\nreturn 0;}\n", headers
        )

    def test_native_reports_coalescing_and_liftoff_without_edge(self):
        self.run_driver(r"""
report(true,100,200);fire_irq();fire_irq();assert(qcount==1);
run_one();assert(samples==1 && output[0].down && output[0].x==204 && output[0].y==148);
assert(timer_pending && timer_ms==20 && !qcount);
report(true,200,300);fire_timer();run_one();
assert(samples==2 && output[1].x==148 && output[1].y==92);
report(false,0,0);fire_timer();run_one();
assert(samples==3 && !output[2].down && output[2].x==148 && output[2].y==92);
assert(!timer_pending && !refs && !s_callback_scheduled);
unsigned before=reads;fire_watchdog();assert(reads==before && !qcount);
""")

    def test_failed_enqueue_and_lost_edge_recovered_without_timer_io(self):
        self.run_driver(r"""
run_one();queue_limit=0;unsigned before=reads;
report(true,10,20);fire_irq();
assert(!qcount && !refs && s_retry_pending && !s_callback_scheduled);
fire_watchdog();assert(!qcount && reads==before && !refs);
queue_limit=8;fire_watchdog();assert(qcount==1 && reads==before);run_one();
assert(output[samples-1].down && !s_retry_pending);
queue_limit=0;fire_timer();assert(!qcount && s_retry_pending && !refs);
queue_limit=8;fire_watchdog();assert(qcount==1);run_one();
report(false,0,0);fire_timer();run_one();assert(!timer_pending);
// INT fell without a delivered ISR: watchdog catches the asserted line.
pad=0;before=reads;fire_watchdog();assert(qcount==1 && reads==before);
run_one();assert(timer_pending && timer_ms==20);
pad=1;fire_timer();run_one();assert(!timer_pending);
""")

    def test_i2c_failure_releases_contact_and_retries_after_reset(self):
        self.run_driver(r"""
report(true,100,200);run_one();fail_read=true;
unsigned baseline_resets=resets;
fire_timer();run_one();assert(!output[samples-1].down && timer_ms==80);
fire_timer();run_one();assert(timer_ms==80);
fire_timer();run_one();assert(timer_ms==1000 && resets==baseline_resets+1);
assert(!s_consecutive_read_failures && irq_enabled);
fail_read=false;report(false,0,0);fire_timer();run_one();
assert(!timer_pending && !output[samples-1].down && uses==releases);
fail_write=true;unsigned before=reads;fire_irq();run_one();
assert(reads==before && timer_pending && timer_ms==80 && uses==releases);
""")

    def test_disable_reenable_rejects_queued_callbacks_and_old_timer(self):
        self.run_driver(r"""
report(true,100,200);run_one();
void (*old_timer)(void *)=timer_cb;void *old_data=timer_data;
fire_irq();assert(qcount==1);
touch_sensor_set_enabled(false);
assert(!irq_enabled && !reset_pin && !timer_pending && !s_watchdog_timer.scheduled);
unsigned before=reads,prior=samples;run_one();assert(reads==before && samples==prior && !refs);
// Re-enable queues new work; stale timer must not clear its coalescing state.
touch_sensor_set_enabled(true);assert(qcount==1 && s_callback_scheduled);
context=TIMER;old_timer(old_data);context=USER;
assert(qcount==1 && s_callback_scheduled);
run_one();assert(samples==prior+1 && output[samples-1].down);
fire_irq();touch_sensor_set_enabled(false);touch_sensor_set_enabled(true);
assert(qcount==2);before=reads;run_one();
assert(reads==before && s_callback_scheduled && qcount==1);
run_one();assert(reads==before+1 && !s_callback_scheduled && !refs);
""")

    def test_interrupt_during_i2c_and_invalid_reports(self):
        self.run_driver(r"""
interrupt_during_read=true;report(true,0,465);run_one();
assert(qcount==1 && refs==1 && output[0].x==259 && output[0].y==0);
run_one();assert(!qcount && !refs);
report(true,466,200);fire_timer();run_one();assert(!output[samples-1].down && !timer_pending);
report(true,100,200);wire[6]=0;fire_irq();run_one();assert(!output[samples-1].down);
report(true,100,200);wire[0]=5;fire_irq();run_one();assert(!output[samples-1].down);
assert(uses==releases && !s_i2c_lock.held && !depth && !input_phase);
""")


if __name__ == "__main__":
    unittest.main()
