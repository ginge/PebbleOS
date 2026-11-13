# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Execute the production interrupt/FIFO driver against a modeled QMI8658 bus."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = [
    "board/board.h",
    "pbl/drivers/accel.h",
    "pbl/drivers/gpio.h",
    "pbl/drivers/intc.h",
    "pbl/util/misc.h",
    "pbl/drivers/i2c.h",
    "pbl/drivers/rtc.h",
    "pbl/kernel/kernel.h",
    "pbl/kernel/irq.h",
    "pbl/kernel/mutex.h",
    "pbl/kernel/sem.h",
    "pbl/kernel/thread.h",
    "pbl/kernel/delay.h",
    "pbl/services/new_timer/new_timer.h",
    "system/status_codes.h",
    "pbl/logging/logging.h",
]
STUB = r"""
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
typedef struct { int16_t x, y, z; uint64_t timestamp_us; } AccelDriverSample;
typedef struct {
 const uint8_t *data; uint16_t num_samples; uint8_t stride;
 struct { uint8_t offset; int8_t sign; } axis[3];
 int32_t scale_num, scale_den; uint64_t first_timestamp_us; uint32_t sampling_interval_us;
} AccelRawBatch;
typedef int IMUCoordinateAxis;
typedef int TimerID;
#define TIMER_INVALID_ID -1
#define TIMER_START_FLAG_REPEATING 1
#define I2C_QMI8658 0
#define PBL_FOREVER -1
#define PBL_PRIO_IDLE 0
#define PBL_PRIO_MAX 4
#define E_ERROR -1
#define S_SUCCESS 0
#define PBL_LOG_MODULE_DEFINE(...)
#define PBL_LOG_DBG(...)
#define PBL_LOG_ERR(...)
struct pbl_mutex { int held; };
struct pbl_sem { int count; };
struct pbl_thread { int unused; };
struct pbl_thread_attr {
 const char *name; void (*entry)(void *); int prio; bool privileged;
 void *stack; size_t stack_size;
};
#define PBL_MUTEX_DEFINE(n) struct pbl_mutex n
#define PBL_SEM_DEFINE(n, i, l) struct pbl_sem n = {i}
#define PBL_THREAD_STACK_DEFINE(n, size) static char n[size]
void pbl_irq_lock(void);
void pbl_irq_unlock(void);
int pbl_mutex_lock(struct pbl_mutex *, int);
void pbl_mutex_unlock(struct pbl_mutex *);
int pbl_sem_take(struct pbl_sem *, int);
void pbl_sem_give(struct pbl_sem *);
int pbl_thread_create(struct pbl_thread *, const struct pbl_thread_attr *);
void rtc_get_time_ms(time_t *, uint16_t *);
void pbl_sleep_ms(unsigned);
TimerID new_timer_create(void);
bool new_timer_start(TimerID,uint32_t,void (*)(void *),void *,int);
bool new_timer_stop(TimerID);

#define PBL_ALIGNED(n) __attribute__((aligned(n)))
#define CONFIG_KERNEL_STACK_ALIGN 8
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct pbl_accel { int unused; };
struct pbl_i2c_dev { int unused; };
struct pbl_gpio { unsigned pin; };
struct pbl_intc { int unused; };
struct pbl_irq_spec { const struct pbl_intc *intc; unsigned hwirq,type; };
#define PBL_GPIO_INPUT 1
#define PBL_ACCEL_EVENT_SHAKE 1
struct pbl_accel_event { int type; IMUCoordinateAxis axis; int32_t direction; };
struct pbl_accel_ops {
 int (*init)(const struct pbl_accel *);
 int (*set_rotated)(const struct pbl_accel *,bool);
 int (*set_sampling_interval)(const struct pbl_accel *,uint32_t,uint32_t *);
 uint32_t (*get_sampling_interval)(const struct pbl_accel *);
 int (*set_num_samples)(const struct pbl_accel *,uint32_t);
 uint32_t (*get_max_num_samples)(const struct pbl_accel *);
 int (*peek)(const struct pbl_accel *,AccelDriverSample *);
 int (*set_shake_detection)(const struct pbl_accel *,bool);
 int (*set_shake_sensitivity_high)(const struct pbl_accel *,bool);
 int (*set_shake_sensitivity_percent)(const struct pbl_accel *,uint8_t);
};
void pbl_mutex_init(struct pbl_mutex *);
void pbl_sem_init(struct pbl_sem *,unsigned,unsigned);
void pbl_accel_lock(const struct pbl_accel *);
void pbl_accel_unlock(const struct pbl_accel *);
void pbl_accel_report_sample(const struct pbl_accel *,const AccelDriverSample *);
void pbl_accel_report_samples(const struct pbl_accel *,const AccelRawBatch *);
void pbl_accel_report_event(const struct pbl_accel *,const struct pbl_accel_event *);
bool new_timer_add_work_callback(void (*)(void *),void *);
void new_timer_delete(TimerID);
int pbl_i2c_write(const struct pbl_i2c_dev *,const uint8_t *,size_t);
int pbl_i2c_burst_read(const struct pbl_i2c_dev *,uint8_t,uint8_t *,unsigned);
int pbl_gpio_get_raw(const struct pbl_gpio *);
int pbl_gpio_configure(const struct pbl_gpio *,uint32_t);
int pbl_intc_request(const struct pbl_irq_spec *,void (*)(void *),void *);
int pbl_intc_free(const struct pbl_irq_spec *);
int pbl_intc_enable(const struct pbl_irq_spec *);
int pbl_intc_disable(const struct pbl_irq_spec *);
"""
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
static bool fifo_read, read_output, inject_irq, command_pending, vary_fifo;
static unsigned ack_reads;
static bool request_pending;
static bool pad_high;
static uint8_t configured_odr, configured_ctrl2, configured_ctrl7, cal_l, cal_h, wom_status;
static unsigned wom_entries, wom_exits, sensor_resets;
static bool hardware_wom, fail_wom_once, stuck_high;
static unsigned append_on_ack;
static unsigned shakes;
static int16_t sample_xraw = 0x2000;
static unsigned reconfigure_before_delivery;
static int set_count(unsigned n);
static int set_shake(bool on);
static bool fail_disable;
static unsigned config_stops;
static uint8_t fail_odr_once;
static uint64_t delivered_last;
static void (*queued)(void *);
static void *queued_data;
#include "drivers/imu/qmi8658/qmi8658_t5ai.c"

static struct pbl_qmi8658_state sensor_state;
static const struct pbl_qmi8658 sensor={.state=&sensor_state,.int2_gpio={.pin=9},.int2={.hwirq=9}};
static unsigned class_locked;
void pbl_mutex_init(struct pbl_mutex *m){m->held=0;}
void pbl_sem_init(struct pbl_sem *s,unsigned count,unsigned limit){(void)limit;s->count=count;}
void pbl_accel_lock(const struct pbl_accel *a){
 (void)a;++class_locked;
 unsigned change=reconfigure_before_delivery; reconfigure_before_delivery=0;
 if(change==1) assert(set_count(1)==0);
 if(change==2) { assert(set_count(0)==0); assert(set_shake(false)==0); }
}
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
int pbl_gpio_get_raw(const struct pbl_gpio *c) { assert(c->pin==9); return pad_high || stuck_high; }
int pbl_intc_disable(const struct pbl_irq_spec *c) { assert(c->hwirq==9); irq_enabled=false; return 0; }
int pbl_intc_enable(const struct pbl_irq_spec *c) { assert(c->hwirq==9); irq_enabled=true; return 0; }
int pbl_gpio_configure(const struct pbl_gpio *c,uint32_t f){assert(c->pin==9);return 0;}
int pbl_intc_request(const struct pbl_irq_spec *c,void (*cb)(void *),void *arg){assert(c->hwirq==9 && cb==prv_int2_handler);return 0;}
int pbl_intc_free(const struct pbl_irq_spec *c){return 0;}
int pbl_i2c_write(const struct pbl_i2c_dev *b, const uint8_t *p, size_t n) {
 (void)b; assert(sensor_state.io_lock.held && !irq_depth && n==2); ++io;
 if(p[0]==QMI8658_CTRL7 && !p[1]) ++config_stops;
 if(p[0]==QMI8658_CTRL2 && fail_odr_once && (p[1]&15)==fail_odr_once) { fail_odr_once=0; return -EIO; }
 if(p[0]==QMI8658_CTRL2) { configured_odr=p[1]&15; configured_ctrl2=p[1]; }
 if(p[0]==QMI8658_CTRL7) configured_ctrl7=p[1];
 if(p[0]==QMI8658_CAL1_L) cal_l=p[1];
 if(p[0]==QMI8658_CAL1_H) cal_h=p[1];
 if(p[0]==QMI8658_RESET) { ++sensor_resets; hardware_wom=false; command_pending=false; ack_reads=0; }
 if(fail_disable && p[0]==QMI8658_CTRL7 && !p[1]) return -EIO;
 if(p[0]==QMI8658_CTRL9) {
  if(!p[1] && command_pending) ack_reads=3;
  if(p[1]) command_pending=true;
 }
 if (p[0]==QMI8658_CTRL9 && p[1]) {
   assert(p[1]==4 || p[1]==5 || p[1]==8); ++commands;
   if(p[1]==8) {
     assert(!configured_ctrl7 && (cal_h>>6)==1);
     hardware_wom=cal_l!=0;
     if(hardware_wom) { assert(cal_l==32 && configured_odr==QMI8658_ODR_LOW_POWER_21HZ && cal_h==0x41); ++wom_entries; }
     else ++wom_exits;
     if(fail_wom_once) { fail_wom_once=false; return -EIO; }
   }
   if(p[1]==5) request_pending=true;
   if(p[1]==4) { assert(configured_odr==QMI8658_ODR_125HZ); fifo_bytes=0; fifo_status=0; }
 }
 if(p[0]==QMI8658_FIFO_CTRL) fifo_read=false;
 return 0;
}
static void raw(uint8_t *p) { p[0]=(uint16_t)sample_xraw; p[1]=(uint16_t)sample_xraw>>8; p[2]=0; p[3]=0x10; p[4]=0; p[5]=0xe0; }
static void stamp(uint8_t *p) { p[0]=counter; p[1]=counter>>8; p[2]=counter>>16; }
int pbl_i2c_burst_read(const struct pbl_i2c_dev *b, uint8_t reg, uint8_t *p, unsigned n) {
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
 case QMI8658_STATUS1: assert(n==1 && hardware_wom); p[0]=wom_status; wom_status=0; pad_high=false; break;
 case QMI8658_CTRL2: assert(n==1); p[0]=configured_ctrl2; break;
 case QMI8658_CTRL7: assert(n==1); p[0]=configured_ctrl7; break;
 case QMI8658_AX_L: read_output=true; raw(p); break;
 case QMI8658_FIFO_COUNT:
   assert(n==2); p[0]=fifo_bytes/2; p[1]=((fifo_bytes/2)>>8)|fifo_status; break;
 case QMI8658_TIMESTAMP: assert(false); break;
 case QMI8658_FIFO_DATA:
   assert(!command_pending && !ack_reads && fifo_read && n==fifo_bytes && n<=sizeof(sensor_state.raw));
   for(unsigned i=0;i<n;i+=6) { raw(p+i); if(vary_fifo) p[i+3]+=i/6; }
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
void pbl_accel_report_event(const struct pbl_accel *a,const struct pbl_accel_event *e) { (void)a; (void)e; assert(class_locked && !sensor_state.io_lock.held); ++shakes; }
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
#ifdef CONFIG_T5AI_POWER_PROFILE
 T5AIQMIPowerStats pm;
 t5ai_qmi8658_power_stats(NULL);t5ai_qmi8658_power_stats(&pm);
 assert(!pm.initialized && !pm.identity_valid && !pm.irqs && !io);
#endif
 pbl_qmi8658_ops.init(&sensor.accel);
#ifdef CONFIG_T5AI_ACCEL_DISABLED
 assert(io==1 && !sensor_state.initialized && !created && !irq_enabled && !timer_ms);
 assert(sensor_state.timer==TIMER_INVALID_ID && !sensor_state.active);
 assert(set_count(1)==-ENODEV && set_shake(true)==-ENODEV);
 set_interval(32000); assert(set_rotated(true)==0);
 AccelDriverSample disabled_sample;
 assert(read_sample(&disabled_sample)==-ENODEV);
 prv_int2_handler(nullptr); prv_timer_callback(NULL); prv_acquire_sample();
 assert(io==1 && !wakes && !queued && !delivered && !timer_ms && !irq_enabled);
 assert(!sensor_state.shake_enabled && !sensor_state.poll_pending);
 return 0;
#endif
#ifdef CONFIG_T5AI_POWER_PROFILE
 unsigned snapshot_io=io;t5ai_qmi8658_power_stats(&pm);
 assert(io==snapshot_io && pm.initialized && pm.identity_valid && !pm.active);
 assert(pm.id==5 && pm.revision==0x7c);
#endif
 assert(sensor_state.initialized && created==1 && !sensor_state.active);
#ifdef QMI8658_CONFIG_TEST
 // Shake and its clamped/restored ODR are applied in one hardware transaction.
 set_interval(333000);
 unsigned stops=config_stops;
 assert(set_shake(true)==0 && config_stops==stops+1);
 assert(sensor_state.interval_us==32000 && sensor_state.requested_interval_us==333000);
 stops=config_stops;
 assert(set_shake(false)==0 && config_stops==stops+1 && sensor_state.interval_us==333000);
 set_count(3); stops=config_stops;
 assert(set_shake(true)==0 && config_stops==stops+1 && sensor_state.watermark==1);
 // Failure at the final ODR restores the whole previous configuration once.
 stops=config_stops; fail_odr_once=QMI8658_ODR_LOW_POWER_3HZ;
 assert(set_shake(false)==-EIO && !fail_odr_once && config_stops==stops+2);
 assert(sensor_state.shake_enabled && sensor_state.num_samples==3);
 assert(sensor_state.interval_us==32000 && sensor_state.requested_interval_us==333000);
 assert(sensor_state.watermark==1 && irq_enabled && !sensor_state.recovery_required);
 // An interval failure must not publish the rejected client request.
 uint32_t actual=0; fail_odr_once=QMI8658_ODR_62_5HZ;
 pbl_accel_lock(&sensor.accel);
 assert(pbl_qmi8658_ops.set_sampling_interval(&sensor.accel,16000,&actual)==-EIO);
 pbl_accel_unlock(&sensor.accel);
 assert(actual==32000 && sensor_state.requested_interval_us==333000);
 assert(sensor_state.shake_enabled && !sensor_state.recovery_required);
 // Rotation drains a queued batch once, keeps the requested FIFO mode, and does no bus I/O.
 set_shake(false); set_interval(8000); now_ms+=100; samples(3); acquire(true);
 unsigned before=delivered, before_io=io, before_commands=commands;
 assert(set_rotated(true)==0 && delivered==before+3 && io==before_io && commands==before_commands);
 assert(!sensor_state.last_sample_valid && sensor_state.watermark==3 && timer_ms==100);
 deliver(); assert(delivered==before+3 && irq_enabled);
 vary_fifo=true;
 now_ms+=100; samples(3); acquire(true); deliver();
 assert(sensor_state.last_sample.x==-562 && sensor_state.last_sample.y==1000);
 AccelDriverSample latest; before_io=io;
 assert(read_sample(&latest)==0 && io==before_io && latest.x==-562);
 // Settling discards only the first part of a batch; raw delivery and newest cache agree.
 samples(3); sensor_state.startup_us=(uint64_t)now_ms*1000u-108000u;
 before=delivered; acquire(true); deliver();
 assert(delivered==before+2 && sensor_state.last_sample.x==-562);
 return 0;
#endif
#ifdef QMI8658_WOM_TEST
 // Quiet shake-only operation enters WOM; streams never do.
 set_interval(32000); set_shake(true); sensor_state.shake_sensitivity_high=true;
 now_ms=65500; // Quiet timing must cross the 16-bit millisecond wrap.
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active && hardware_wom && irq_enabled && timer_ms==5000);
 assert(wom_entries==1 && !queued && !delivered && sensor_state.shake_baseline_valid);
 const int32_t old_baseline=sensor_state.shake_baseline[0];
 // Quiet health passes neither fabricate samples/shakes nor recover a healthy sensor.
 unsigned before_shakes=shakes, before_io=io;
 acquire(false);
 assert(io==before_io+3 && sensor_state.wom_active && !sensor_state.recovery_required);
 assert(shakes==before_shakes && !sensor_state.poll_pending && irq_enabled);
 // A stuck-high pad allows one catchup, never a read loop.
 stuck_high=true; acquire(false);
 assert(sensor_state.poll_signal.count==1);
 pbl_sem_take(&sensor_state.poll_signal,PBL_FOREVER); prv_acquire_sample();
 assert(!sensor_state.poll_signal.count && !sensor_state.poll_pending && irq_enabled);
 stuck_high=false;
 // Lost edge with latched STATUS1 is recovered on the health pass.
 wom_status=4; pad_high=true; acquire(false);
 assert(!hardware_wom && !sensor_state.wom_active && wom_exits==1 && timer_ms==100);
 assert(sensor_state.shake_baseline_valid && sensor_state.shake_baseline[0]==old_baseline);
 assert(shakes==before_shakes && !queued); // WOM alone is not a shake.
 sample_xraw=0x4000; now_ms+=110; samples(1); acquire(true);
 assert(shakes==before_shakes+1); // First settled motion can use the pre-WOM baseline.
 // Return to WOM, then a data subscriber must exit before enabling the FIFO.
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active);
 set_count(3); assert(!sensor_state.wom_active && !hardware_wom && sensor_state.watermark==1);
 set_shake(false); assert(sensor_state.watermark==3);
 for(unsigned i=0;i<4;++i) { now_ms+=1500; samples(3); acquire(true); deliver(); }
 assert(!sensor_state.wom_active);
 // Rotation while in WOM retains its qualified exit/restart behavior.
 set_count(0); set_shake(true);
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active);
 assert(set_rotated(true)==0 && !sensor_state.wom_active && !hardware_wom);
 assert(!sensor_state.shake_baseline_valid && !sensor_state.last_sample_valid);
 before_io=io; assert(set_rotated(false)==0 && io==before_io);
 // Fresh peek cannot return the last pre-WOM sample as current data.
 set_count(0); set_shake(true);
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active); before_io=io;
 AccelDriverSample peek_wom; assert(read_sample(&peek_wom)==0);
 assert(io>before_io && read_output && !hardware_wom && !sensor_state.wom_active);
 // Disabling from WOM must fully exit and stop all sensor work.
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active); set_shake(false);
 assert(!hardware_wom && !sensor_state.wom_active && !irq_enabled && !timer_ms);
 set_shake(true);
 // Partial entry is undone and failure suppresses re-entry until reboot.
 fail_wom_once=true;
 for(unsigned i=0;i<200 && !sensor_state.recovery_required;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_failed && sensor_state.wom_configured && sensor_state.recovery_required && !irq_enabled);
 acquire(false); assert(!hardware_wom && !sensor_state.wom_configured && !sensor_state.recovery_required);
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(!sensor_state.wom_active && irq_enabled);
 // A failed exit resets only the sensor, then restores the requested stream.
 sensor_state.wom_failed=false;
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active);
 fail_wom_once=true; unsigned before_resets=sensor_resets;
 set_count(3); assert(sensor_resets==before_resets+1 && !hardware_wom && !sensor_state.wom_active);
 assert(sensor_state.wom_failed && sensor_state.watermark==1 && irq_enabled);
 // A reset/corrupted control register is detected by the WOM health check.
 set_count(0); sensor_state.wom_failed=false;
 for(unsigned i=0;i<200;++i) { samples(1); acquire(true); }
 assert(sensor_state.wom_active); configured_ctrl7=0;
 acquire(false); assert(sensor_state.recovery_required && sensor_state.wom_failed && !irq_enabled && timer_ms==100);
 acquire(false); assert(!hardware_wom && !sensor_state.wom_active && !sensor_state.recovery_required);
 set_shake(false); assert(!sensor_state.active && !irq_enabled && !timer_ms);
 return 0;
#endif

 // A first shake subscriber has no implicit 125 Hz data-client request.
 assert(set_shake(true)==0 && sensor_state.interval_us==32000);
 assert(sensor_state.shake_enabled && !sensor_state.num_samples && sensor_state.watermark==1);
 assert(set_shake(false)==0 && !sensor_state.active && !irq_enabled && !timer_ms);
 // Explicit high-rate requests still override the initial shake-only rate.
 assert(set_interval(8000)==8000);
 assert(set_shake(true)==0 && sensor_state.interval_us==8000);
 assert(set_shake(false)==0 && !sensor_state.active);
 set_count(1); assert(sensor_state.active && sensor_state.watermark==1 && irq_enabled && timer_ms==100);
 now_ms+=100; samples(1); inject_irq=true; acquire(true); inject_irq=false;
 // An edge during I/O must survive until delivery releases the raw slot.
 assert(queued && irq_enabled);
 void (*direct_cb)(void *)=queued; queued=NULL; direct_cb(queued_data);
 assert(delivered==1 && sensor_state.poll_pending && sensor_state.poll_signal.count==1);
 pbl_sem_take(&sensor_state.poll_signal,PBL_FOREVER); prv_acquire_sample();
 // Same counter is suppressed; a high pad alone cannot keep work running.
 assert(!queued && !sensor_state.poll_pending && !sensor_state.poll_signal.count && irq_enabled);
#ifdef CONFIG_T5AI_POWER_PROFILE
 snapshot_io=io;t5ai_qmi8658_power_stats(&pm);
 assert(io==snapshot_io && pm.irqs>=2 && pm.passes==2 && pm.acquired==1);
 assert(pm.active && pm.watermark==1 && pm.irq_armed && pm.batch==1);
#endif
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
 // Rotation is software-only, including in standby; it cannot cause a bus fault.
 old=io; fail_disable=true; assert(set_rotated(true)==0 && io==old);
 assert(!sensor_state.recovery_required && !timer_ms); set_rotated(false);
 // Configuration failure while inactive must also start recovery.
 assert(set_count(1)==-EIO && sensor_state.num_samples==0);
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
 // Counter wrap does not duplicate timestamps; rotation invalidates old samples without I/O.
 set_interval(8000); now_ms+=100; counter=0xfffffe; samples(3);
 acquire(true); deliver(); samples(3); acquire(true); deliver();
 old=io; set_rotated(true); assert(!sensor_state.last_sample_valid && io==old);
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
 // Shake-only acquisition bypasses the offload queue, including when full.
 set_count(0); set_interval(32000); set_shake(true);
 now_ms+=100; samples(1); old=shakes; full=true; acquire(true);
 assert(!queued && !sensor_state.poll_pending && irq_enabled && sensor_state.last_sample_valid);
 sample_xraw=0x4000; samples(1); acquire(true);
 assert(shakes==old+1 && !queued && !sensor_state.poll_pending && irq_enabled);
 samples(1); acquire(true); assert(shakes==old+1); full=false;
 // A data client joining between the I/O unlock and device lock invalidates
 // the old raw slot, then receives subsequent samples through NewTimers.
 samples(1); old=delivered; reconfigure_before_delivery=1; acquire(true);
 assert(!reconfigure_before_delivery && sensor_state.num_samples==1 && queued);
 deliver(); assert(delivered==old);
 now_ms+=100; samples(1); acquire(true); assert(queued); deliver(); assert(delivered==old+1);
 // Several pulses during deferred delivery coalesce into one follow-up.
 samples(1); acquire(true); old=delivered; samples(1);
 for(unsigned i=0;i<3;++i) { assert(irq_enabled); prv_int2_handler(nullptr); }
 assert(!sensor_state.poll_signal.count);
 direct_cb=queued; queued=NULL; direct_cb(queued_data);
 assert(delivered==old+1 && sensor_state.poll_signal.count==1);
 pbl_sem_take(&sensor_state.poll_signal,PBL_FOREVER); prv_acquire_sample();
 pad_high=true; deliver(); pad_high=false;
 assert(delivered==old+2 && !sensor_state.poll_signal.count);
 // A transfer failure masks direct pulses until the bounded recovery pass.
 samples(1); fail_read=true; inject_irq=true; acquire(true);
 fail_read=false; inject_irq=false;
 assert(sensor_state.recovery_required && !irq_enabled && !queued && !sensor_state.poll_signal.count);
 acquire(false); assert(!sensor_state.recovery_required && irq_enabled && !sensor_state.direct_edge_pending);
 // Disable discards a busy pulse and cannot schedule stale raw-slot work.
 samples(1); acquire(true); prv_int2_handler(nullptr); set_count(0); set_shake(false);
 deliver(); assert(!sensor_state.poll_signal.count && !irq_enabled);
 set_shake(true);
 // Disabling in the same window cannot re-arm the interrupt or a timer.
 set_count(0); now_ms+=100; samples(1); reconfigure_before_delivery=2; acquire(true);
 assert(!queued && !sensor_state.active && !sensor_state.poll_pending && !irq_enabled && !timer_ms);
 return 0;
}
"""


class QMI8658FifoTest(unittest.TestCase):
    def test_interrupt_fifo_transitions_and_recovery(self):
        self.run_driver([])

    def test_sparse_power_state(self):
        self.run_driver(["-DCONFIG_T5AI_POWER_PROFILE=1"])

    def test_wom_entry_exit_clients_and_fallback(self):
        self.run_driver(["-DCONFIG_T5AI_QMI8658_WOM=1", "-DQMI8658_WOM_TEST=1"])

    def test_wom_with_existing_transitions(self):
        self.run_driver(["-DCONFIG_T5AI_QMI8658_WOM=1"])

    def test_configuration_transactions_and_software_rotation(self):
        self.run_driver(["-DCONFIG_T5AI_QMI8658_WOM=1", "-DQMI8658_CONFIG_TEST=1"])

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
                    "-Wextra",
                    "-Wno-unused-parameter",
                    "-Wno-unused-function",
                    "-Werror",
                    "-Wno-misleading-indentation",
                    "-fsanitize=undefined,address",
                    *flags,
                    "-I",
                    str(d),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
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
