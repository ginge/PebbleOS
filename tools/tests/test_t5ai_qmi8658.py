# SPDX-License-Identifier: Apache-2.0
"""Exercise the production QMI8658 polling pipeline with a controlled scheduler."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = [
    "board/board.h",
    "pbl/drivers/accel.h", "pbl/drivers/gpio.h", "pbl/drivers/intc.h", "pbl/util/misc.h",
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
void pbl_i2c_use(const struct pbl_i2c_dev *);
void pbl_i2c_release(const struct pbl_i2c_dev *);
int pbl_i2c_write_block(const struct pbl_i2c_dev *,size_t,const uint8_t *);
int pbl_i2c_read_block(const struct pbl_i2c_dev *,size_t,uint8_t *);
int pbl_i2c_read_register_block(const struct pbl_i2c_dev *,uint8_t,unsigned,uint8_t *);
int pbl_gpio_get_raw(const struct pbl_gpio *);
int pbl_gpio_configure(const struct pbl_gpio *,uint32_t);
int pbl_intc_request(const struct pbl_irq_spec *,void (*)(void *),void *);
int pbl_intc_free(const struct pbl_irq_spec *);
int pbl_intc_enable(const struct pbl_irq_spec *);
int pbl_intc_disable(const struct pbl_irq_spec *);
"""
HARNESS = r"""
#include "stub.h"
#include <assert.h>
#include <string.h>
static unsigned irq_depth, io, delivered, wakes, created;
static uint8_t last_reg, last_value;
static unsigned timers_created, timers_started;
static bool ready = true, fail_read, full, tick_in_read, invalidate_in_read;
static void (*queued)(void *);
static void *queued_data;
static unsigned now_ms = 200;
#include "drivers/imu/qmi8658/qmi8658_t5ai_legacy.c"

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
 (void)t; assert(!irq_depth && !m->held); m->held = 1; return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(m->held); m->held = 0; }
int pbl_sem_take(struct pbl_sem *s, int t) { (void)t; assert(s->count); --s->count; return 0; }
void pbl_sem_give(struct pbl_sem *s) { assert(!irq_depth); assert(!s->count); ++s->count; ++wakes; }
int pbl_thread_create(struct pbl_thread *t, const struct pbl_thread_attr *a) {
 (void)t; assert(a->entry == prv_poll_entry && a->prio == 3 && a->privileged);
 ++created; return 0;
}
void pbl_i2c_use(const struct pbl_i2c_dev *b) { (void)b; assert(sensor_state.io_lock.held && !irq_depth); }
void pbl_i2c_release(const struct pbl_i2c_dev *b) { (void)b; }
int pbl_i2c_write_block(const struct pbl_i2c_dev *b, size_t n, const uint8_t *p) {
 (void)b; last_reg = p[0]; if (n == 2) last_value=p[1]; ++io; return 0;
}
int pbl_i2c_read_block(const struct pbl_i2c_dev *b, size_t n, uint8_t *p) {
 (void)b; ++io;
 if (tick_in_read) { unsigned old = wakes; prv_timer_callback(NULL); assert(wakes == old); }
 if (invalidate_in_read) { prv_invalidate_poll(); invalidate_in_read = false; }
 memset(p, 0, n);
 if (n == 1) *p = last_reg == QMI8658_WHO_AM_I ? QMI8658_ID : (ready ? 1 : 0);
 else { p[1] = 0x20; p[3] = 0x10; p[5] = 0xe0; }
 return fail_read ? -EIO : 0;
}
void rtc_get_time_ms(time_t *s, uint16_t *ms) { *s = now_ms / 1000; *ms = now_ms % 1000; }
void pbl_sleep_ms(unsigned ms) { now_ms += ms; }
TimerID new_timer_create(void) { ++timers_created; return 1; }
bool new_timer_start(TimerID t, uint32_t ms, void (*cb)(void *), void *d, int f) {
 (void)t; (void)ms; (void)cb; (void)d; (void)f; ++timers_started; return true;
}
bool new_timer_stop(TimerID t) { (void)t; return true; }
bool new_timer_add_work_callback(void (*cb)(void *),void *data) { assert(!irq_depth); if (full) return false;
 assert(!queued); queued = cb; queued_data=data; return true;
}
void pbl_accel_report_sample(const struct pbl_accel *a,const AccelDriverSample *s) {
 assert(s->x == 500 && s->y == -1000 && s->z == -1000);
 assert(sensor_state.poll_pending); ++delivered;
 unsigned old = wakes; prv_timer_callback(NULL); assert(wakes == old);
}
void pbl_accel_report_event(const struct pbl_accel *a,const struct pbl_accel_event *e) { (void)a; (void)e; assert(class_locked); }
static void acquire(void) {
 unsigned old = io;
 prv_timer_callback(NULL); assert(io == old); // Timer never touches I2C.
 assert(sensor_state.poll_signal.count == 1);
 pbl_sem_take(&sensor_state.poll_signal, PBL_FOREVER); prv_acquire_sample();
}
static void deliver(void) {
 assert(queued); void (*cb)(void *) = queued; queued = NULL;
 unsigned old = io; cb(queued_data); assert(io == old); // NewTimers delivery never touches I2C.
 assert(!sensor_state.poll_pending && !irq_depth);
}
int main(void) {
#ifdef CONFIG_T5AI_ACCEL_DISABLED
 pbl_qmi8658_ops.init(&sensor.accel);
 assert(io==1 && last_reg==QMI8658_CTRL7 && last_value==0);
 assert(!sensor_state.initialized && !sensor_state.active && !created && !timers_created && !timers_started);
 set_count(1); set_shake(true);
 set_interval(32000); set_rotated(true);
 AccelDriverSample sample;
 assert(read_sample(&sample)==-ENODEV);
 prv_timer_callback(NULL); prv_acquire_sample();
 assert(io==1 && !wakes && !queued && !delivered && !timers_started);
 assert(!sensor_state.shake_enabled && !sensor_state.poll_pending);
 return 0;
#endif
 pbl_qmi8658_ops.init(&sensor.accel); assert(sensor_state.initialized && created == 1);
 sensor_state.active = true; sensor_state.num_samples = 1;
 tick_in_read = true;
 acquire(); assert(sensor_state.poll_pending && queued); deliver(); assert(delivered == 1);
 // Queue pressure drops a sample and permits a fresh acquisition next tick.
 full = true; acquire(); assert(!sensor_state.poll_pending && !queued); full = false;
 acquire(); deliver(); assert(delivered == 2);
 // No-ready and bus failures do not leave the pipeline stuck.
 ready = false; acquire(); assert(!sensor_state.poll_pending && !queued); ready = true;
 fail_read = true; acquire(); assert(!sensor_state.poll_pending && !queued); fail_read = false;
 // Reconfiguration during a transfer or queued delivery rejects stale data.
 invalidate_in_read = true; acquire(); assert(!sensor_state.poll_pending && !queued);
 acquire(); prv_invalidate_poll(); deliver(); assert(delivered == 2);
 // Power-off while delivery is queued cannot deliver an old sample.
 acquire(); sensor_state.active = false; deliver(); assert(delivered == 2);
 unsigned old = wakes; prv_timer_callback(NULL); assert(wakes == old);
 sensor_state.active = true; sensor_state.startup_us = now_ms * 1000;
 acquire(); deliver(); assert(delivered == 2); // Startup transient suppressed.
 now_ms += 100; acquire(); deliver(); assert(delivered == 3);
 return 0;
}
"""


class QMI8658PollingTest(unittest.TestCase):
    def test_bounded_worker_pipeline(self):
        self.run_pipeline([])

    def test_disabled_has_no_acquisition_or_delivery(self):
        self.run_pipeline(["-DCONFIG_T5AI_ACCEL_DISABLED=1"])

    def run_pipeline(self, flags):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "stub.h").write_text(STUB)
            for name in HEADERS:
                header = directory / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "stub.h"\n')
            harness = directory / "test.c"
            harness.write_text(HARNESS)
            binary = directory / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=gnu2x", "-DCONFIG_T5AI_QMI8658_LEGACY=1",
                    "-Wall",
                    "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
                    "-Werror",
                    *flags,
                    "-I",
                    str(directory),
                    "-I",
                    str(ROOT),
                    "-I", str(ROOT / "include"),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
