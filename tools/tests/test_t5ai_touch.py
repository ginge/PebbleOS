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
#define PBL_INPUT_GES_PALM 2
#define TIMER_INVALID_ID 0
#define PBL_MUTEX_DEFINE(name) struct pbl_mutex name
#define PBL_GPIO(dev,p,f) {.pin=(p)}
#define PBL_IRQ_SPEC(dev,p,f) {.pin=(p)}
#define I2C_CST92XX ((void *)1)
#define nullptr NULL
#define PBL_MSEC(ms) (ms)
#define pbl_ms_to_ticks(ms) (ms)
struct pbl_work {void (*fn)(struct pbl_work *);bool pending;};
struct pbl_delayed_work {struct pbl_work work;bool scheduled;unsigned id;};
#define PBL_WORK_DEFINE(name,fn) struct pbl_work name={fn,false}
static int pbl_sys_workq,pbl_sys_workq_high;
struct pbl_device {int unused;};
static const struct {struct pbl_device dev;} T5AI_GPIO_INTC;
struct pbl_touch {int unused;};
static const struct pbl_touch test_touch;
#define TOUCH (&test_touch)
struct pbl_touch_ops {
 int (*init)(const struct pbl_touch *);
 int (*enable)(const struct pbl_touch *);
 int (*disable)(const struct pbl_touch *);
};
int pbl_device_init(const struct pbl_device *dev) {assert(dev==&T5AI_GPIO_INTC.dev);return 0;}
struct pbl_gpio {unsigned pin;};
struct pbl_irq_spec {unsigned pin;};
struct pbl_mutex {bool held;};
enum {USER,BG,TIMER,ISR};
unsigned context,depth,queue_limit=8,qcount,refs,reads,writes;
unsigned sleeps,resets,timer_creates,timer_ms,samples,enqueue_calls,acks;
unsigned gestures,sequence,palm_sequence,last_sample_sequence;
bool fail_ack;
void check_input_lock(void);
void test_set_enabled(bool enabled);
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
int pbl_i2c_write(const void *p,const uint8_t *data,unsigned n){
 assert(p==I2C_CST92XX && context!=TIMER && context!=ISR && !depth && data[0]==0xd0 && data[1]==0);
 if(n==3){assert(data[2]==0xab);++acks;return fail_ack?-1:0;}
 assert(n==2);++writes;return fail_write?-1:0;
}
void fire_irq(void){unsigned saved=context;context=ISR;irq_cb(NULL);context=saved;}
int pbl_i2c_read(const void *p,uint8_t *data,unsigned n){
 assert(p==I2C_CST92XX && n==10);++reads;
 if(interrupt_during_read){fire_irq();fire_irq();interrupt_during_read=false;}
 memcpy(data,wire,10);return fail_read?-1:0;
}
static void delayed_cb(void *data){struct pbl_delayed_work *t=data;t->work.fn(&t->work);}
void pbl_delayed_work_init(struct pbl_delayed_work *t,void (*fn)(struct pbl_work *)){
 t->work.fn=fn;t->id=++timer_creates;
}
bool pbl_delayed_work_reschedule(int *q,struct pbl_delayed_work *t,unsigned ms){
 assert(q==&pbl_sys_workq_high && !depth);t->scheduled=true;
 if(t->id==1){timer_ms=ms;timer_cb=delayed_cb;timer_data=t;timer_pending=true;}
 return true;
}
bool pbl_delayed_work_schedule(int *q,struct pbl_delayed_work *t,unsigned ms){
 return pbl_delayed_work_reschedule(q,t,ms);
}
bool pbl_delayed_work_schedule_next(struct pbl_delayed_work *t,unsigned ms){
 assert(ms==1000);t->scheduled=true;return true;
}
bool pbl_delayed_work_cancel(struct pbl_delayed_work *t){
 t->scheduled=false;if(t->id==1)timer_pending=false;return true;
}
static void work_cb(void *arg){struct pbl_work *w=arg;w->pending=false;w->fn(w);}
bool pbl_work_submit_boosted(int *q,struct pbl_work *w){
 assert(q==&pbl_sys_workq && depth);++enqueue_calls;
 if(w->pending)return false;
 assert(qcount<16);w->pending=true;
 queue[qcount].cb=work_cb;queue[qcount++].data=w;++refs;return true;
}
void pbl_input_report_key(unsigned code,bool down,bool sync){
 assert(context==BG && !depth && code==4 && !sync && !input_phase);
 pending.down=down;input_phase=1;
}
void pbl_input_report_abs(unsigned code,int value,bool sync){
 assert(context==BG && !depth);
 if(code==0){assert((input_phase==0 || input_phase==1) && !sync);pending.x=value;input_phase=2;}
 else {assert(code==1 && input_phase==2);pending.y=value;
  if(sync){assert(samples<64);output[samples++]=pending;input_phase=0;last_sample_sequence=++sequence;}
  else {input_phase=3;}
 }
}
void pbl_input_report_ges(unsigned code,bool sync){
 check_input_lock();assert(context==BG && !depth && input_phase==3 && sync && code==2);
 input_phase=0;++gestures;palm_sequence=++sequence;
}
void pbl_touch_report(const struct pbl_touch *touch,bool down,unsigned x,unsigned y) {
 assert(touch==TOUCH);pbl_input_report_key(4,down,false);
 pbl_input_report_abs(0,x,false);pbl_input_report_abs(1,y,true);
}
void pbl_touch_report_gesture(const struct pbl_touch *touch,unsigned code,unsigned x,unsigned y) {
 assert(touch==TOUCH);pbl_input_report_abs(0,x,false);
 pbl_input_report_abs(1,y,false);pbl_input_report_ges(code,true);
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include DRIVER_SOURCE
#pragma GCC diagnostic pop
void test_set_enabled(bool enabled) {assert((enabled?pbl_touch_cst92xx_t5ai_ops.enable:pbl_touch_cst92xx_t5ai_ops.disable)(TOUCH)==0);}
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
 assert(s_watchdog_timer.scheduled);context=TIMER;s_watchdog_timer.work.fn(&s_watchdog_timer.work);context=USER;
}
void report(bool down,unsigned x,unsigned y){
 memset(wire,0,10);wire[0]=6;wire[5]=down;wire[6]=0xab;
 wire[1]=x>>4;wire[2]=y>>4;wire[3]=(x&15)<<4 | (y&15);
}
void setup(void){
 report(false,0,0);assert(pbl_touch_cst92xx_t5ai_ops.init(TOUCH)==0);assert(pbl_touch_cst92xx_t5ai_ops.init(TOUCH)==0);
 assert(timer_creates==2 && reads==1 && !reset_pin && !irq_enabled);
 test_set_enabled(true);
 assert(irq_enabled && reset_pin && qcount==1 && refs==1 && s_watchdog_timer.scheduled);
}
void check_input_lock(void){
 // Listener-side service locks must never be entered holding the driver's lock.
 assert(!s_i2c_lock.held);
}
"""


class T5AITouchTest(unittest.TestCase):
    def run_driver(self, checks, gesture_hook=""):
        headers = {
            name: ""
            for name in re.findall(r'#include [<"]([^>"\n]+)[>"]', DRIVER.read_text())
            if "/" in name
        }
        headers["pbl/soc/t5ai/draw_profile.h"] = (
            ROOT / "include/pbl/soc/t5ai/draw_profile.h"
        ).read_text()
        source = HARNESS.replace("DRIVER_SOURCE", '"' + str(DRIVER) + '"')
        source = source.replace(
            "input_phase=0;++gestures;palm_sequence=++sequence;",
            "input_phase=0;++gestures;palm_sequence=++sequence;" + gesture_hook,
        )
        # Assert lock ordering at the synchronous input boundary.
        source = source.replace(
            "void pbl_input_report_key(unsigned code,bool down,bool sync){",
            "void check_input_lock(void);\nvoid pbl_input_report_key(unsigned code,bool down,bool sync){check_input_lock();",
        )
        run_c(
            source + "\nint main(void){setup();\n" + checks + "\nreturn 0;}\n", headers
        )

    def test_stuck_idle_int_backs_off_but_real_edges_and_contacts_stay_fast(self):
        self.run_driver(r"""
pad=0;run_one();assert(timer_ms==20 && !output[0].down);
const unsigned delays[]={40,80,160,320,640,1000,1000};
for(unsigned i=0;i<sizeof(delays)/sizeof(delays[0]);++i){
 unsigned before=reads;fire_watchdog();assert(!qcount && reads==before);
 fire_timer();run_one();assert(timer_ms==delays[i] && !output[samples-1].down);
}
// A genuine falling edge bypasses idle backoff without waiting for the timer.
report(true,100,200);fire_irq();assert(qcount==1);run_one();
assert(output[samples-1].down && timer_ms==20 && !s_idle_poll_ms);
// Held and suppressed contacts never use idle backoff.
for(unsigned i=0;i<3;++i){fire_timer();run_one();assert(timer_ms==20);}
wire[4]=0x80;fire_timer();run_one();assert(s_palm_down && timer_ms==20);
report(false,0,0);fire_timer();run_one();assert(!s_palm_down && timer_ms==20);
pad=1;fire_timer();run_one();assert(!timer_pending && !s_idle_poll_ms);
// Long idle does no bus I/O; first touch is dispatched immediately.
unsigned before=reads;for(unsigned i=0;i<20;++i)fire_watchdog();
assert(reads==before && !qcount);
report(true,200,300);fire_irq();run_one();assert(output[samples-1].down && timer_ms==20);
""")

    def test_idle_ack_echo_backoff_resets_across_failure_and_enable(self):
        self.run_driver(r"""
pad=0;wire[0]=0xab;wire[5]=0;run_one();
fire_timer();run_one();assert(timer_ms==40 && !output[samples-1].down);
fire_timer();run_one();assert(timer_ms==80);
fail_ack=true;fire_timer();run_one();assert(timer_ms==80 && !s_idle_poll_ms);
fail_ack=false;fire_timer();run_one();assert(timer_ms==20 && !s_transfer_retry);
fire_timer();run_one();assert(timer_ms==40);
test_set_enabled(false);test_set_enabled(false);
assert(!timer_pending && !s_idle_poll_ms && !s_transfer_retry && !reset_pin);
test_set_enabled(true);test_set_enabled(true);run_one();assert(timer_ms==20);
""")

    def test_failed_transfer_irq_storm_cannot_bypass_retry_or_reset_delay(self):
        self.run_driver(r"""
report(true,100,200);run_one();fail_read=true;interrupt_during_read=true;
fire_timer();run_one();assert(qcount==1 && timer_ms==80 && s_transfer_retry);
unsigned before=reads,baseline=resets;
// Work already queued by an IRQ during I2C also respects the retry delay.
run_one();assert(reads==before && timer_pending && !s_callback_scheduled);
for(unsigned i=0;i<100;++i)fire_irq();
fire_watchdog();assert(!qcount && reads==before && resets==baseline);
fire_timer();run_one();assert(reads==before+1 && timer_ms==80);
fire_timer();run_one();assert(timer_ms==1000 && resets==baseline+1);
before=reads;for(unsigned i=0;i<100;++i)fire_irq();
fire_watchdog();assert(!qcount && reads==before && timer_pending);
// Retry enqueue rejection remains recoverable through the regular watchdog.
fire_timer();assert(qcount==1 && !s_transfer_retry);
fail_read=false;report(false,0,0);fire_watchdog();run_one();
assert(!timer_pending && !s_transfer_retry && !output[samples-1].down);
report(true,20,30);fire_irq();run_one();assert(output[samples-1].down);
""")

    def test_disable_during_failure_rejects_stale_retry_and_repeated_cycles(self):
        self.run_driver(r"""
fail_write=true;run_one();assert(s_transfer_retry && timer_ms==80);

for(unsigned i=0;i<10;++i){
 test_set_enabled(false);unsigned before=reads;
 fire_irq();assert(!qcount && reads==before && !timer_pending && !reset_pin);
 test_set_enabled(true);assert(qcount==1 && !s_transfer_retry);

 assert(qcount==1 && s_callback_scheduled);
 fail_write=false;report(false,0,0);run_one();assert(!timer_pending);
}
""")

    def test_palm_listener_disable_cancels_following_raw_sample(self):
        self.run_driver(
            r"""
report(true,100,200);run_one();assert(samples==1 && output[0].down);
wire[4]=0x80;fire_timer();run_one();
assert(gestures==1 && samples==1 && !s_enabled && !reset_pin && !timer_pending);
report(false,0,0);test_set_enabled(true);run_one();
assert(samples==2 && !output[1].down && !s_palm_down && !timer_pending);
""",
            gesture_hook="test_set_enabled(false);",
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

    def test_embedded_work_coalesces_and_recovers_lost_edges_without_timer_io(self):
        self.run_driver(r"""
run_one();unsigned before=reads;
report(true,10,20);fire_irq();fire_irq();fire_watchdog();
assert(qcount==1 && refs==1 && reads==before);run_one();
assert(output[samples-1].down);
fire_timer();assert(qcount==1);fire_irq();assert(qcount==1);run_one();
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
assert(!timer_pending && !output[samples-1].down && !s_i2c_lock.held);
fail_write=true;unsigned before=reads;fire_irq();run_one();
assert(reads==before && timer_pending && timer_ms==80 && !s_i2c_lock.held);
""")

    def test_disable_reenable_rejects_queued_callbacks_and_old_timer(self):
        self.run_driver(r"""
report(true,100,200);run_one();

fire_irq();assert(qcount==1);
test_set_enabled(false);
assert(!irq_enabled && !reset_pin && !timer_pending && !s_watchdog_timer.scheduled);
unsigned before=reads,prior=samples;run_one();assert(reads==before && samples==prior && !refs);
// Re-enable queues new work; stale timer must not clear its coalescing state.
test_set_enabled(true);assert(qcount==1 && s_callback_scheduled);

assert(qcount==1 && s_callback_scheduled);
run_one();assert(samples==prior+1 && output[samples-1].down);
fire_irq();test_set_enabled(false);test_set_enabled(true);
assert(qcount==1);before=reads;run_one();
assert(reads==before+1 && !s_callback_scheduled && !refs);
""")

    def test_production_ack_and_ack_failure_recovery(self):
        self.run_driver(r"""
assert(acks==1);report(true,100,200);run_one();assert(acks==2 && output[0].down);
fail_ack=true;fire_timer();run_one();
assert(!output[1].down && timer_ms==80 && s_consecutive_read_failures==1);
fail_ack=false;fire_timer();run_one();
assert(output[2].down && !s_consecutive_read_failures && !s_i2c_lock.held);
""")

    def test_palm_once_suppresses_coordinates_until_all_contacts_lift(self):
        self.run_driver(r"""
report(true,100,200);wire[4]=0x80;run_one();
assert(gestures==1 && !output[0].down && palm_sequence<last_sample_sequence);
assert(timer_pending && timer_ms==20 && s_palm_down);
fire_timer();run_one();assert(gestures==1 && !output[1].down);
// A remaining finger cannot start a new raw contact during the cover episode.
report(true,200,300);fire_timer();run_one();assert(!output[2].down && s_palm_down);
// First slot lifted, but a second contact is still active.
wire[0]=0;wire[5]=2;wire[7]=0x16;fire_timer();run_one();
assert(s_palm_down && timer_pending && gestures==1 && !output[3].down);
wire[5]=1;wire[7]=0;fire_timer();run_one();
assert(!s_palm_down && !timer_pending && !output[4].down);
report(true,200,300);fire_irq();run_one();assert(output[5].down && gestures==1);
""")

    def test_palm_after_contact_and_invalid_cover_flags(self):
        self.run_driver(r"""
report(true,100,200);run_one();assert(output[0].down);
wire[4]=0x80;fire_timer();run_one();
assert(gestures==1 && !output[1].down && palm_sequence<last_sample_sequence);
fail_read=true;fire_timer();run_one();assert(s_palm_down && gestures==1);
fail_read=false;fire_timer();run_one();assert(gestures==1 && !output[3].down);
report(false,0,0);fire_timer();run_one();assert(!s_palm_down);
report(true,100,200);wire[4]=0x80;wire[6]=0;
fire_irq();run_one();assert(gestures==1 && !s_palm_down);
wire[6]=0xab;wire[0]=0xab;fire_irq();run_one();assert(gestures==1);
report(true,100,200);wire[4]=0x80;fire_irq();run_one();assert(gestures==2);
test_set_enabled(false);assert(!s_palm_down && !s_down);
test_set_enabled(true);run_one();assert(gestures==3);
""")

    def test_ack_echo_does_not_synthesize_liftoff(self):
        self.run_driver(r"""
report(true,100,200);run_one();
wire[0]=0xab;wire[5]=0;fire_timer();run_one();
assert(output[1].down && output[1].x==output[0].x && timer_pending);
report(false,0,0);fire_timer();run_one();assert(!output[2].down && !timer_pending);
""")

    def test_interrupt_during_i2c_and_invalid_reports(self):
        self.run_driver(r"""
interrupt_during_read=true;report(true,0,465);run_one();
assert(qcount==1 && refs==1 && output[0].x==259 && output[0].y==0);
run_one();assert(!qcount && !refs);
report(true,466,200);fire_timer();run_one();assert(!output[samples-1].down && !timer_pending);
report(true,100,200);wire[6]=0;fire_irq();run_one();assert(!output[samples-1].down);
report(true,100,200);wire[0]=5;fire_irq();run_one();assert(!output[samples-1].down);
assert(!s_i2c_lock.held && !s_i2c_lock.held && !depth && !input_phase);
""")


if __name__ == "__main__":
    unittest.main()
