# SPDX-License-Identifier: Apache-2.0
"""Execute the production interrupt/FIFO driver against a modeled QMI8658 bus."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.tests.test_t5ai_qmi8658 import HEADERS, STUB

ROOT = Path(__file__).resolve().parents[2]
EXTRA = r"""
#define PBL_LOG_WRN(...) ((void)0)
uint64_t rtc_get_ticks(void);
"""
HARNESS = r"""
#include "stub.h"
#include <assert.h>
#include <string.h>
static unsigned irq_depth, io, delivered, batches, wakes, created, timer_ms, commands;
static uint32_t now_ms = 1000, counter = 100;
static unsigned fifo_bytes, fifo_status;
static bool irq_enabled, command_done = true, ready = true, fail_read, full;
static bool fifo_read, read_output, inject_irq, command_pending;
static unsigned ack_reads;
static bool request_pending;
static bool pad_high;
static uint8_t configured_odr;
static unsigned append_on_ack;
static unsigned shakes;
static bool fail_disable;
static uint64_t delivered_last;
static void (*queued)(void *);
static void *queued_data;
#include "drivers/imu/qmi8658/qmi8658_t5ai.c"

static struct pbl_qmi8658_state sensor_state;
static const struct pbl_qmi8658 sensor={.state=&sensor_state,.int2_gpio={.pin=9},.int2={.hwirq=9}};
static unsigned class_locked;
void pbl_mutex_init(struct pbl_mutex *m){m->held=0;}
void pbl_sem_init(struct pbl_sem *s,unsigned count,unsigned limit){(void)limit;s->count=count;}
void pbl_accel_lock(const struct pbl_accel *a){(void)a;++class_locked;}
void pbl_accel_unlock(const struct pbl_accel *a){(void)a;assert(class_locked);--class_locked;}
void new_timer_delete(TimerID id){(void)id;}
static int set_count(unsigned n){pbl_accel_lock(&sensor.accel);int ret=pbl_qmi8658_ops.set_num_samples(&sensor.accel,n);pbl_accel_unlock(&sensor.accel);return ret;}
static int set_rotated(bool on){pbl_accel_lock(&sensor.accel);int ret=pbl_qmi8658_ops.set_rotated(&sensor.accel,on);pbl_accel_unlock(&sensor.accel);return ret;}
static int set_shake(bool on){pbl_accel_lock(&sensor.accel);int ret=pbl_qmi8658_ops.set_shake_detection(&sensor.accel,on);pbl_accel_unlock(&sensor.accel);return ret;}
static unsigned set_interval(unsigned us){uint32_t actual;pbl_accel_lock(&sensor.accel);assert(pbl_qmi8658_ops.set_sampling_interval(&sensor.accel,us,&actual)==0);pbl_accel_unlock(&sensor.accel);return actual;}
static int read_sample(AccelDriverSample *sample){pbl_accel_lock(&sensor.accel);int ret=pbl_qmi8658_ops.peek(&sensor.accel,sample);pbl_accel_unlock(&sensor.accel);return ret;}
void pbl_irq_lock(void) { ++irq_depth; }
void pbl_irq_unlock(void) { assert(irq_depth); --irq_depth; }
int pbl_mutex_lock(struct pbl_mutex *m, int t) {
 (void)t; assert(!irq_depth && !m->held); m->held=1; return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(m->held); m->held=0; }
int pbl_sem_take(struct pbl_sem *s, int t) { (void)t; assert(s->count); --s->count; return 0; }
void pbl_sem_give(struct pbl_sem *s) { assert(!irq_depth); assert(!s->count); ++s->count; ++wakes; }
int pbl_thread_create(struct pbl_thread *t, const struct pbl_thread_attr *a) {
 (void)t; assert(a->prio==3 && a->entry==prv_poll_entry); ++created; return 0;
}
int pbl_gpio_get_raw(const struct pbl_gpio *c) { assert(c->pin==9); return pad_high; }
int pbl_intc_disable(const struct pbl_irq_spec *c) { assert(c->hwirq==9); irq_enabled=false; return 0; }
int pbl_intc_enable(const struct pbl_irq_spec *c) { assert(c->hwirq==9); irq_enabled=true; return 0; }
int pbl_gpio_configure(const struct pbl_gpio *c,uint32_t f){assert(c->pin==9);return 0;}
int pbl_intc_request(const struct pbl_irq_spec *c,void (*cb)(void *),void *arg){assert(c->hwirq==9 && cb==prv_int2_handler);return 0;}
int pbl_intc_free(const struct pbl_irq_spec *c){return 0;}
void pbl_i2c_use(const struct pbl_i2c_dev *b) { (void)b; assert(sensor_state.io_lock.held && !irq_depth); }
void pbl_i2c_release(const struct pbl_i2c_dev *b) { (void)b; }
int pbl_i2c_write_block(const struct pbl_i2c_dev *b, size_t n, const uint8_t *p) {
 (void)b; assert(n==2); ++io;
 if(p[0]==QMI8658_CTRL2) configured_odr=p[1]&15;
 if(fail_disable && p[0]==QMI8658_CTRL7 && !p[1]) return -EIO;
 if(p[0]==QMI8658_CTRL9) {
  if(!p[1] && command_pending) ack_reads=3;
  if(p[1]) command_pending=true;
 }
 if (p[0]==QMI8658_CTRL9 && p[1]) {
   assert(p[1]==4 || p[1]==5); ++commands;
   if(p[1]==5) request_pending=true;
   if(p[1]==4) { assert(configured_odr==QMI8658_ODR_125HZ); fifo_bytes=0; fifo_status=0; }
 }
 if(p[0]==QMI8658_FIFO_CTRL) fifo_read=false;
 return 0;
}
static void raw(uint8_t *p) { p[0]=0; p[1]=0x20; p[2]=0; p[3]=0x10; p[4]=0; p[5]=0xe0; }
static void stamp(uint8_t *p) { p[0]=counter; p[1]=counter>>8; p[2]=counter>>16; }
int pbl_i2c_read_register_block(const struct pbl_i2c_dev *b, uint8_t reg, unsigned n, uint8_t *p) {
 (void)b; ++io; memset(p,0,n);
 if(inject_irq) { unsigned old=wakes; prv_int2_handler(nullptr); assert(wakes==old); }
 if(fail_read) return -EIO;
 switch(reg) {
 case QMI8658_WHO_AM_I: assert(n==1); p[0]=5; break;
 case QMI8658_WHO_AM_I+1: assert(n==1); p[0]=0x7c; break;
 case QMI8658_STATUSINT:
   if(request_pending && ack_reads==2 && append_on_ack) {
     fifo_bytes+=6*append_on_ack; now_ms+=sensor_state.interval_us*append_on_ack/1000;
     append_on_ack=0;
   }
   if(ack_reads && !--ack_reads) {
     command_pending=false;
     if(request_pending) fifo_read=true;
     request_pending=false;
   }
   *p=(command_pending && command_done) ? 0x80 : 0; break;
 case QMI8658_STATUS0:
   *p=ready;
   if(n==13) { stamp(p+2); raw(p+7); } break;
 case QMI8658_AX_L: read_output=true; raw(p); break;
 case QMI8658_FIFO_COUNT:
   assert(n==2); p[0]=fifo_bytes/2; p[1]=((fifo_bytes/2)>>8)|fifo_status; break;
 case QMI8658_TIMESTAMP: assert(false); break;
 case QMI8658_FIFO_DATA:
   assert(!command_pending && !ack_reads && fifo_read && n==fifo_bytes && n<=sizeof(sensor_state.raw));
   for(unsigned i=0;i<n;i+=6) raw(p+i);
   fifo_bytes=0; fifo_status=0; break;
 default: assert(false);
 }
 return 0;
}
void rtc_get_time_ms(time_t *s, uint16_t *ms) { *s=now_ms/1000; *ms=now_ms%1000; }
uint64_t rtc_get_ticks(void) { return now_ms; }
void pbl_sleep_ms(unsigned ms) { now_ms+=ms; }
TimerID new_timer_create(void) { return 1; }
bool new_timer_start(TimerID t, uint32_t ms, void (*cb)(void *), void *d, int f) {
 (void)t; (void)d; (void)f; assert(cb==prv_timer_callback); timer_ms=ms; return true;
}
bool new_timer_stop(TimerID t) { (void)t; timer_ms=0; return true; }
bool new_timer_add_work_callback(void (*cb)(void *),void *data) {
 if(full) return false; assert(!queued); queued=cb; queued_data=data; return true;
}
void pbl_accel_report_sample(const struct pbl_accel *a,const AccelDriverSample *s) { (void)s; assert(false); }
void pbl_accel_report_samples(const struct pbl_accel *a,const AccelRawBatch *b) {
 assert(!irq_depth); ++batches;
 assert(b->stride==6 && b->scale_num==1000 && b->scale_den==8192);
 assert(b->axis[0].offset==2 && b->axis[1].offset==0 && b->axis[2].offset==4);
 for(unsigned i=0;i<b->num_samples;++i) {
  uint64_t ts=b->first_timestamp_us+(uint64_t)i*b->sampling_interval_us;
  assert(!delivered || ts>delivered_last); delivered_last=ts; ++delivered;
 }
}
void pbl_accel_report_event(const struct pbl_accel *a,const struct pbl_accel_event *e) { (void)a; (void)e; assert(class_locked); ++shakes; }
static void acquire(bool interrupt) {
 unsigned old=io;
 if(interrupt) prv_int2_handler(nullptr); else { now_ms+=timer_ms; prv_timer_callback(NULL); }
 assert(io==old && sensor_state.poll_signal.count==1);
 pbl_sem_take(&sensor_state.poll_signal,PBL_FOREVER); prv_acquire_sample();
}
static void deliver(void) {
 assert(queued); void (*cb)(void *)=queued; queued=NULL;
 unsigned old=io; cb(queued_data); assert(io==old && !sensor_state.poll_pending);
}
static void samples(unsigned n) {
 now_ms+=sensor_state.interval_us*n/1000; counter+=n;
 fifo_bytes=n*6; fifo_status=0x40;
}
int main(void) {
 pbl_qmi8658_ops.init(&sensor.accel);
#ifdef CONFIG_T5AI_ACCEL_DISABLED
 assert(!sensor_state.initialized && !created && !irq_enabled); return 0;
#endif
 assert(sensor_state.initialized && created==1 && !sensor_state.active);
 set_count(1); assert(sensor_state.active && sensor_state.watermark==1 && irq_enabled && timer_ms==100);
 now_ms+=100; samples(1); inject_irq=true; acquire(true); inject_irq=false;
 assert(queued && !irq_enabled); deliver(); assert(delivered==1 && irq_enabled);
 assert(sensor_state.last_sample.x==500 && sensor_state.last_sample.y==-1000 && sensor_state.last_sample.z==-1000);
 AccelDriverSample peek; unsigned old=io;
 assert(read_sample(&peek)==S_SUCCESS && io==old);
 // Batch acquisition follows the subscriber limit and does not read outputs.
 set_count(3); assert(sensor_state.watermark==3);
 now_ms+=100; samples(3); acquire(true); assert(sensor_state.pending_count==3 && !fifo_read);
 deliver(); assert(delivered==4 && !read_output);
 // FIFO watermark deassertion may lag the resume write. This is not a new
 // batch and must neither issue a zero-byte read nor enter recovery.
 samples(3); acquire(true); pad_high=true;
 void (*settle_cb)(void *)=queued; queued=NULL; settle_cb(queued_data);
 assert(!sensor_state.poll_pending && !sensor_state.poll_signal.count && !sensor_state.recovery_required);
 pad_high=false;
 fifo_bytes=0; fifo_status=0; acquire(true);
 assert(!queued && !sensor_state.recovery_required && !sensor_state.poll_pending && !fifo_read);
 // Watermark rises while the delivery slot masks the edge. Catch up once,
 // without waiting for the 100 ms watchdog or looping on a stuck-high pad.
 samples(3); acquire(true);
 samples(3); pad_high=true;
 void (*cb)(void *)=queued; queued=NULL; cb(queued_data);
 assert(sensor_state.poll_pending && sensor_state.poll_signal.count==1 && !irq_enabled);
 pbl_sem_take(&sensor_state.poll_signal,PBL_FOREVER); prv_acquire_sample();
 cb=queued; queued=NULL; cb(queued_data);
 assert(!sensor_state.poll_pending && !sensor_state.poll_signal.count && irq_enabled);
 pad_high=false;
 // This silicon freezes the output counter in FIFO mode; batches remain distinct.
 unsigned frozen_counter=counter; samples(3); counter=frozen_counter;
 acquire(true); deliver(); assert(delivered==16);
 // Lowering a batch limit flushes queued delivery once; the callback is stale.
 samples(3); acquire(true); old=delivered; set_count(1);
 assert(delivered==old+3); deliver(); assert(delivered==old+3);
 now_ms+=100; samples(1); acquire(true); old=delivered;
 // Disable drains already acquired data, and stale callback cannot duplicate it.
 set_count(0); assert(delivered==old+1 && !sensor_state.active && !irq_enabled && !timer_ms);
 deliver(); old=wakes; prv_int2_handler(nullptr); assert(wakes==old);
 // Single-shot peek restores standby; never consumes active FIFO contents.
 assert(read_sample(&peek)==S_SUCCESS && read_output && sensor_state.last_sample_valid);
 // A failed standby write gets bounded retries; successful recovery stops the timer.
 fail_disable=true; assert(read_sample(&peek)==-EIO && sensor_state.recovery_required && timer_ms);
 fail_disable=false; acquire(false);
 assert(!sensor_state.active && !sensor_state.recovery_required && !timer_ms && !irq_enabled);
 // Configuration failure while inactive must also start recovery.
 fail_disable=true; set_rotated(true);
 assert(sensor_state.recovery_required && timer_ms);
 fail_disable=false; acquire(false); assert(!timer_ms); set_rotated(false);
 set_count(3); now_ms+=100; samples(3);
 full=true; acquire(true); full=false; assert(!queued && !sensor_state.poll_pending && irq_enabled);
 samples(3); acquire(true); deliver();
 // Malformed FIFO and overflow force a bounded recovery on the next watchdog.
 for(unsigned fault=0;fault<4;++fault) {
  samples(3);
  if(fault==0) fifo_bytes+=2;
  if(fault==1) fifo_status|=0x20;
  if(fault==2) fifo_bytes=7*6;
  if(fault==3) command_done=false;
  old=commands; acquire(true);
  assert(!queued && !sensor_state.poll_pending && sensor_state.recovery_required && !irq_enabled && !fifo_read);
  assert(commands-old<=1); command_done=true;
  acquire(false); assert(!sensor_state.recovery_required && irq_enabled && !queued);
  now_ms+=100; samples(3); acquire(true); deliver();
 }
 fail_read=true; acquire(true); assert(sensor_state.recovery_required && !queued); fail_read=false;
 acquire(false); assert(!sensor_state.recovery_required);
 now_ms+=100; samples(3); acquire(false); deliver(); // Missed edge/stuck high watchdog.
 // Software shake keeps per-sample latency and its rate clamp.
 set_shake(true); assert(sensor_state.watermark==1);
 assert(set_interval(333000)==32000);
 now_ms+=100; samples(1); acquire(true); deliver();
 set_shake(false); assert(sensor_state.interval_us==333000 && sensor_state.watermark==3);
 const uint32_t request[]={8000,16000,32000,48000,91000,333000};
 for(unsigned i=0;i<6;++i) assert(set_interval(request[i])==request[i]);
 // Counter wrap does not duplicate timestamps; rotation invalidates old samples.
 set_interval(8000); now_ms+=100; counter=0xfffffe; samples(3);
 acquire(true); deliver(); samples(3); acquire(true); deliver();
 set_rotated(true); assert(!sensor_state.last_sample_valid);
 now_ms+=100; samples(3); acquire(true); deliver();
 assert(sensor_state.last_sample.x==-500 && sensor_state.last_sample.y==1000 && sensor_state.last_sample.z==-1000);
 // Startup transient suppression also applies after recovery/reconfiguration.
 set_interval(16000); samples(1); acquire(true); if(queued) deliver();
 assert(!sensor_state.last_sample_valid && !irq_depth);
 // Samples arriving while CTRL9 completes belong to this drain. The count
 // must be read again after the handshake, rather than using the IRQ count.
 now_ms+=100; samples(3); append_on_ack=1; old=delivered;
 acquire(true); assert(sensor_state.pending_count==4 && !fifo_read);
 deliver(); assert(delivered==old+4 && !append_on_ack);
 // A watchdog without watermark must not freeze the FIFO unnecessarily.
 fifo_bytes=0; fifo_status=0; old=commands;
 acquire(false); assert(commands==old && !queued && !sensor_state.recovery_required);
 // Shake filter threshold and holdoff retain the existing behavior.
 sensor_state.shake_enabled=true; sensor_state.shake_baseline_valid=false; sensor_state.shake_sensitivity_high=true;
 AccelDriverSample a={.z=1000,.timestamp_us=10000000}; pbl_accel_lock(&sensor.accel); prv_detect_shake(&a); pbl_accel_unlock(&sensor.accel);
 a.x=300; a.timestamp_us+=32000; pbl_accel_lock(&sensor.accel); prv_detect_shake(&a); pbl_accel_unlock(&sensor.accel); assert(shakes==1);
 a.x=-300; a.timestamp_us+=32000; pbl_accel_lock(&sensor.accel); prv_detect_shake(&a); pbl_accel_unlock(&sensor.accel); assert(shakes==1);
 a.timestamp_us+=500000; pbl_accel_lock(&sensor.accel); prv_detect_shake(&a); pbl_accel_unlock(&sensor.accel); // Long gaps reset the baseline.
 return 0;
}
"""


class QMI8658FifoTest(unittest.TestCase):
    def test_interrupt_fifo_transitions_and_recovery(self):
        self.run_driver([])

    def test_disabled(self):
        self.run_driver(["-DCONFIG_T5AI_ACCEL_DISABLED=1"])

    def run_driver(self, flags):
        with tempfile.TemporaryDirectory() as temp:
            d = Path(temp)
            (d / "stub.h").write_text(STUB + EXTRA)
            for name in HEADERS + ["pbl/drivers/intc.h", "pbl/drivers/gpio.h"]:
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "stub.h"\n')
            (d / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu2x",
                    "-Wall",
                    "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
                    "-Werror",
                    "-Wno-misleading-indentation",
                    "-fsanitize=undefined,address",
                    *flags,
                    "-I",
                    str(d),
                    "-I",
                    str(ROOT),
                    "-I", str(ROOT / "include"),
                    str(d / "test.c"),
                    "-o",
                    str(d / "test"),
                ],
                check=True,
            )
            subprocess.run(
                [str(d / "test")],
                check=True,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
            )


if __name__ == "__main__":
    unittest.main()
