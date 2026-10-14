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
typedef struct { unsigned pin, pull; } ExtiConfig;
typedef struct { unsigned pin; } InputConfig;
bool gpio_input_read(const InputConfig *);
#define BOARD_QMI8658_INT2 ((ExtiConfig){9, 0})
#define ExtiTrigger_Rising 0
void exti_disable(ExtiConfig);
void exti_enable(ExtiConfig);
void exti_configure_pin(ExtiConfig, int, void (*)(void));
bool i2c_read_register_block(int, uint8_t, unsigned, uint8_t *);
typedef struct {
 const uint8_t *data; uint16_t num_samples; uint8_t stride;
 struct { uint8_t offset; int8_t sign; } axis[3];
 int32_t scale_num, scale_den; uint64_t first_timestamp_us; uint32_t sampling_interval_us;
} AccelRawBatch;
void accel_cb_new_samples(const AccelRawBatch *);
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
static void (*queued)(void);
#include "fw/drivers/imu/qmi8658/qmi8658_t5ai.c"
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
bool gpio_input_read(const InputConfig *c) { assert(c->pin==9); return pad_high; }
void exti_disable(ExtiConfig c) { assert(c.pin==9); irq_enabled=false; }
void exti_enable(ExtiConfig c) { assert(c.pin==9); irq_enabled=true; }
void exti_configure_pin(ExtiConfig c, int edge, void (*cb)(void)) {
 assert(c.pin==9 && edge==0 && cb==prv_int2_handler);
}
void i2c_use(int b) { (void)b; assert(s_io_lock.held && !irq_depth); }
void i2c_release(int b) { (void)b; }
bool i2c_write_block(int b, size_t n, const uint8_t *p) {
 (void)b; assert(n==2); ++io;
 if(p[0]==QMI8658_CTRL2) configured_odr=p[1]&15;
 if(fail_disable && p[0]==QMI8658_CTRL7 && !p[1]) return false;
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
 return true;
}
static void raw(uint8_t *p) { p[0]=0; p[1]=0x20; p[2]=0; p[3]=0x10; p[4]=0; p[5]=0xe0; }
static void stamp(uint8_t *p) { p[0]=counter; p[1]=counter>>8; p[2]=counter>>16; }
bool i2c_read_register_block(int b, uint8_t reg, unsigned n, uint8_t *p) {
 (void)b; ++io; memset(p,0,n);
 if(inject_irq) { unsigned old=wakes; prv_int2_handler(); assert(wakes==old); }
 if(fail_read) return false;
 switch(reg) {
 case QMI8658_WHO_AM_I: assert(n==1); p[0]=5; break;
 case QMI8658_WHO_AM_I+1: assert(n==1); p[0]=0x7c; break;
 case QMI8658_STATUSINT:
   if(request_pending && ack_reads==2 && append_on_ack) {
     fifo_bytes+=6*append_on_ack; now_ms+=s_interval_us*append_on_ack/1000;
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
   assert(!command_pending && !ack_reads && fifo_read && n==fifo_bytes && n<=sizeof(s_raw));
   for(unsigned i=0;i<n;i+=6) raw(p+i);
   fifo_bytes=0; fifo_status=0; break;
 default: assert(false);
 }
 return true;
}
void rtc_get_time_ms(time_t *s, uint16_t *ms) { *s=now_ms/1000; *ms=now_ms%1000; }
uint64_t rtc_get_ticks(void) { return now_ms; }
void psleep(unsigned ms) { now_ms+=ms; }
TimerID new_timer_create(void) { return 1; }
bool new_timer_start(TimerID t, uint32_t ms, void (*cb)(void *), void *d, int f) {
 (void)t; (void)d; (void)f; assert(cb==prv_timer_callback); timer_ms=ms; return true;
}
bool new_timer_stop(TimerID t) { (void)t; timer_ms=0; return true; }
bool accel_offload_work(void (*cb)(void)) {
 if(full) return false; assert(!queued); queued=cb; return true;
}
void accel_cb_new_sample(const AccelDriverSample *s) { (void)s; assert(false); }
void accel_cb_new_samples(const AccelRawBatch *b) {
 assert(!irq_depth); ++batches;
 assert(b->stride==6 && b->scale_num==1000 && b->scale_den==8192);
 assert(b->axis[0].offset==2 && b->axis[1].offset==0 && b->axis[2].offset==4);
 for(unsigned i=0;i<b->num_samples;++i) {
  uint64_t ts=b->first_timestamp_us+(uint64_t)i*b->sampling_interval_us;
  assert(!delivered || ts>delivered_last); delivered_last=ts; ++delivered;
 }
}
void accel_cb_shake_detected(IMUCoordinateAxis a, int d) { (void)a; (void)d; ++shakes; }
static void acquire(bool interrupt) {
 unsigned old=io;
 if(interrupt) prv_int2_handler(); else { now_ms+=timer_ms; prv_timer_callback(NULL); }
 assert(io==old && s_poll_signal.count==1);
 pbl_sem_take(&s_poll_signal,PBL_FOREVER); prv_acquire_sample();
}
static void deliver(void) {
 assert(queued); void (*cb)(void)=queued; queued=NULL;
 unsigned old=io; cb(); assert(io==old && !s_poll_pending);
}
static void samples(unsigned n) {
 now_ms+=s_interval_us*n/1000; counter+=n;
 fifo_bytes=n*6; fifo_status=0x40;
}
int main(void) {
 accel_init();
#ifdef CONFIG_T5AI_ACCEL_DISABLED
 assert(!s_initialized && !created && !irq_enabled); return 0;
#endif
 assert(s_initialized && created==1 && !s_active);
 accel_set_num_samples(1); assert(s_active && s_watermark==1 && irq_enabled && timer_ms==100);
 now_ms+=100; samples(1); inject_irq=true; acquire(true); inject_irq=false;
 assert(queued && !irq_enabled); deliver(); assert(delivered==1 && irq_enabled);
 assert(s_last_sample.x==500 && s_last_sample.y==-1000 && s_last_sample.z==-1000);
 AccelDriverSample peek; unsigned old=io;
 assert(accel_peek(&peek)==S_SUCCESS && io==old);
 // Batch acquisition follows the subscriber limit and does not read outputs.
 accel_set_num_samples(3); assert(s_watermark==3);
 now_ms+=100; samples(3); acquire(true); assert(s_pending_count==3 && !fifo_read);
 deliver(); assert(delivered==4 && !read_output);
 // FIFO watermark deassertion may lag the resume write. This is not a new
 // batch and must neither issue a zero-byte read nor enter recovery.
 samples(3); acquire(true); pad_high=true;
 void (*settle_cb)(void)=queued; queued=NULL; settle_cb();
 assert(!s_poll_pending && !s_poll_signal.count && !s_recovery_required);
 pad_high=false;
 fifo_bytes=0; fifo_status=0; acquire(true);
 assert(!queued && !s_recovery_required && !s_poll_pending && !fifo_read);
 // Watermark rises while the delivery slot masks the edge. Catch up once,
 // without waiting for the 100 ms watchdog or looping on a stuck-high pad.
 samples(3); acquire(true);
 samples(3); pad_high=true;
 void (*cb)(void)=queued; queued=NULL; cb();
 assert(s_poll_pending && s_poll_signal.count==1 && !irq_enabled);
 pbl_sem_take(&s_poll_signal,PBL_FOREVER); prv_acquire_sample();
 cb=queued; queued=NULL; cb();
 assert(!s_poll_pending && !s_poll_signal.count && irq_enabled);
 pad_high=false;
 // This silicon freezes the output counter in FIFO mode; batches remain distinct.
 unsigned frozen_counter=counter; samples(3); counter=frozen_counter;
 acquire(true); deliver(); assert(delivered==16);
 // Lowering a batch limit flushes queued delivery once; the callback is stale.
 samples(3); acquire(true); old=delivered; accel_set_num_samples(1);
 assert(delivered==old+3); deliver(); assert(delivered==old+3);
 now_ms+=100; samples(1); acquire(true); old=delivered;
 // Disable drains already acquired data, and stale callback cannot duplicate it.
 accel_set_num_samples(0); assert(delivered==old+1 && !s_active && !irq_enabled && !timer_ms);
 deliver(); old=wakes; prv_int2_handler(); assert(wakes==old);
 // Single-shot peek restores standby; never consumes active FIFO contents.
 assert(accel_peek(&peek)==S_SUCCESS && read_output && s_last_sample_valid);
 // A failed standby write gets bounded retries; successful recovery stops the timer.
 fail_disable=true; assert(accel_peek(&peek)==E_ERROR && s_recovery_required && timer_ms);
 fail_disable=false; acquire(false);
 assert(!s_active && !s_recovery_required && !timer_ms && !irq_enabled);
 // Configuration failure while inactive must also start recovery.
 fail_disable=true; accel_set_rotated(true);
 assert(s_recovery_required && timer_ms);
 fail_disable=false; acquire(false); assert(!timer_ms); accel_set_rotated(false);
 accel_set_num_samples(3); now_ms+=100; samples(3);
 full=true; acquire(true); full=false; assert(!queued && !s_poll_pending && irq_enabled);
 samples(3); acquire(true); deliver();
 // Malformed FIFO and overflow force a bounded recovery on the next watchdog.
 for(unsigned fault=0;fault<4;++fault) {
  samples(3);
  if(fault==0) fifo_bytes+=2;
  if(fault==1) fifo_status|=0x20;
  if(fault==2) fifo_bytes=7*6;
  if(fault==3) command_done=false;
  old=commands; acquire(true);
  assert(!queued && !s_poll_pending && s_recovery_required && !irq_enabled && !fifo_read);
  assert(commands-old<=1); command_done=true;
  acquire(false); assert(!s_recovery_required && irq_enabled && !queued);
  now_ms+=100; samples(3); acquire(true); deliver();
 }
 fail_read=true; acquire(true); assert(s_recovery_required && !queued); fail_read=false;
 acquire(false); assert(!s_recovery_required);
 now_ms+=100; samples(3); acquire(false); deliver(); // Missed edge/stuck high watchdog.
 // Software shake keeps per-sample latency and its rate clamp.
 accel_enable_shake_detection(true); assert(s_watermark==1);
 assert(accel_set_sampling_interval(333000)==32000);
 now_ms+=100; samples(1); acquire(true); deliver();
 accel_enable_shake_detection(false); assert(s_interval_us==333000 && s_watermark==3);
 const uint32_t request[]={8000,16000,32000,48000,91000,333000};
 for(unsigned i=0;i<6;++i) assert(accel_set_sampling_interval(request[i])==request[i]);
 // Counter wrap does not duplicate timestamps; rotation invalidates old samples.
 accel_set_sampling_interval(8000); now_ms+=100; counter=0xfffffe; samples(3);
 acquire(true); deliver(); samples(3); acquire(true); deliver();
 accel_set_rotated(true); assert(!s_last_sample_valid);
 now_ms+=100; samples(3); acquire(true); deliver();
 assert(s_last_sample.x==-500 && s_last_sample.y==1000 && s_last_sample.z==-1000);
 // Startup transient suppression also applies after recovery/reconfiguration.
 accel_set_sampling_interval(16000); samples(1); acquire(true); if(queued) deliver();
 assert(!s_last_sample_valid && !irq_depth);
 // Samples arriving while CTRL9 completes belong to this drain. The count
 // must be read again after the handshake, rather than using the IRQ count.
 now_ms+=100; samples(3); append_on_ack=1; old=delivered;
 acquire(true); assert(s_pending_count==4 && !fifo_read);
 deliver(); assert(delivered==old+4 && !append_on_ack);
 // A watchdog without watermark must not freeze the FIFO unnecessarily.
 fifo_bytes=0; fifo_status=0; old=commands;
 acquire(false); assert(commands==old && !queued && !s_recovery_required);
 // Shake filter threshold and holdoff retain the existing behavior.
 s_shake_enabled=true; s_shake_baseline_valid=false; s_shake_sensitivity_high=true;
 AccelDriverSample a={.z=1000,.timestamp_us=10000000}; prv_detect_shake(&a);
 a.x=300; a.timestamp_us+=32000; prv_detect_shake(&a); assert(shakes==1);
 a.x=-300; a.timestamp_us+=32000; prv_detect_shake(&a); assert(shakes==1);
 a.timestamp_us+=500000; prv_detect_shake(&a); // Long gaps reset the baseline.
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
            for name in HEADERS + ["pbl/drivers/exti.h", "pbl/drivers/gpio.h"]:
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "stub.h"\n')
            (d / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-misleading-indentation",
                    "-fsanitize=undefined,address",
                    *flags,
                    "-I",
                    str(d),
                    "-I",
                    str(ROOT),
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
