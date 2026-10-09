# SPDX-License-Identifier: Apache-2.0
"""Exercise the production QMI8658 polling pipeline with a controlled scheduler."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADERS = [
    "board/board.h", "pbl/drivers/accel.h", "pbl/drivers/i2c.h",
    "pbl/drivers/rtc.h", "pbl/kernel/kernel.h", "pbl/kernel/irq.h",
    "pbl/kernel/mutex.h", "pbl/kernel/sem.h", "pbl/kernel/thread.h",
    "kernel/util/sleep.h", "pbl/services/new_timer/new_timer.h",
    "pbl/services/system_task.h", "system/status_codes.h", "pbl/logging/logging.h",
]
STUB = r"""
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
typedef struct { int16_t x, y, z; uint64_t timestamp_us; } AccelDriverSample;
typedef int IMUCoordinateAxis;
typedef int TimerID;
#define TIMER_INVALID_ID -1
#define TIMER_START_FLAG_REPEATING 1
#define I2C_QMI8658 0
#define PBL_FOREVER -1
#define PBL_PRIO_IDLE 0
#define E_ERROR -1
#define S_SUCCESS 0
#define PBL_LOG_MODULE_DEFINE(...)
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
void i2c_use(int);
void i2c_release(int);
bool i2c_write_block(int, size_t, const uint8_t *);
bool i2c_read_block(int, size_t, uint8_t *);
void rtc_get_time_ms(time_t *, uint16_t *);
void psleep(unsigned);
TimerID new_timer_create(void);
bool new_timer_start(TimerID, uint32_t, void (*)(void *), void *, int);
bool new_timer_stop(TimerID);
bool system_task_add_callback_droppable(void (*)(void *), void *);
void accel_cb_new_sample(const AccelDriverSample *);
void accel_cb_shake_detected(IMUCoordinateAxis, int);
"""
HARNESS = r"""
#include "stub.h"
#include <assert.h>
#include <string.h>
static unsigned irq_depth, io, delivered, wakes;
static bool ready = true, fail_read, full, tick_in_read, invalidate_in_read;
static void (*queued)(void *);
static unsigned now_ms = 200;
#include "fw/drivers/imu/qmi8658/qmi8658_t5ai.c"
void pbl_irq_lock(void) { ++irq_depth; }
void pbl_irq_unlock(void) { assert(irq_depth); --irq_depth; }
int pbl_mutex_lock(struct pbl_mutex *m, int t) {
 (void)t; assert(!irq_depth && !m->held); m->held = 1; return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m) { assert(m->held); m->held = 0; }
int pbl_sem_take(struct pbl_sem *s, int t) { (void)t; assert(s->count); --s->count; return 0; }
void pbl_sem_give(struct pbl_sem *s) { assert(!irq_depth); assert(!s->count); ++s->count; ++wakes; }
int pbl_thread_create(struct pbl_thread *t, const struct pbl_thread_attr *a) {
 (void)t; assert(a->entry == prv_poll_entry && a->prio == 2); return 0;
}
void i2c_use(int b) { (void)b; assert(s_io_lock.held && !irq_depth); }
void i2c_release(int b) { (void)b; }
bool i2c_write_block(int b, size_t n, const uint8_t *p) {
 (void)b; (void)n; (void)p; ++io; return true;
}
bool i2c_read_block(int b, size_t n, uint8_t *p) {
 (void)b; ++io;
 if (tick_in_read) { unsigned old = wakes; prv_timer_callback(NULL); assert(wakes == old); }
 if (invalidate_in_read) { prv_invalidate_poll(); invalidate_in_read = false; }
 memset(p, 0, n);
 if (n == 1) *p = ready ? 1 : 0;
 else { p[1] = 0x20; p[3] = 0x10; p[5] = 0xe0; }
 return !fail_read;
}
void rtc_get_time_ms(time_t *s, uint16_t *ms) { *s = now_ms / 1000; *ms = now_ms % 1000; }
void psleep(unsigned ms) { now_ms += ms; }
TimerID new_timer_create(void) { return 1; }
bool new_timer_start(TimerID t, uint32_t ms, void (*cb)(void *), void *d, int f) {
 (void)t; (void)ms; (void)cb; (void)d; (void)f; return true;
}
bool new_timer_stop(TimerID t) { (void)t; return true; }
bool system_task_add_callback_droppable(void (*cb)(void *), void *d) {
 (void)d; assert(!irq_depth); if (full) return false;
 assert(!queued); queued = cb; return true;
}
void accel_cb_new_sample(const AccelDriverSample *s) {
 assert(s->x == 500 && s->y == -1000 && s->z == -1000);
 assert(s_poll_pending); ++delivered;
 unsigned old = wakes; prv_timer_callback(NULL); assert(wakes == old);
}
void accel_cb_shake_detected(IMUCoordinateAxis a, int d) { (void)a; (void)d; }
static void acquire(void) {
 unsigned old = io;
 prv_timer_callback(NULL); assert(io == old); // Timer never touches I2C.
 assert(s_poll_signal.count == 1);
 pbl_sem_take(&s_poll_signal, PBL_FOREVER); prv_acquire_sample();
}
static void deliver(void) {
 assert(queued); void (*cb)(void *) = queued; queued = NULL;
 unsigned old = io; cb(NULL); assert(io == old); // KernelBG never touches I2C.
 assert(!s_poll_pending && !irq_depth);
}
int main(void) {
 s_initialized = s_active = true; s_num_samples = 1;
 tick_in_read = true;
 acquire(); assert(s_poll_pending && queued); deliver(); assert(delivered == 1);
 // Queue pressure drops a sample and permits a fresh acquisition next tick.
 full = true; acquire(); assert(!s_poll_pending && !queued); full = false;
 acquire(); deliver(); assert(delivered == 2);
 // No-ready and bus failures do not leave the pipeline stuck.
 ready = false; acquire(); assert(!s_poll_pending && !queued); ready = true;
 fail_read = true; acquire(); assert(!s_poll_pending && !queued); fail_read = false;
 // Reconfiguration during a transfer or queued delivery rejects stale data.
 invalidate_in_read = true; acquire(); assert(!s_poll_pending && !queued);
 acquire(); prv_invalidate_poll(); deliver(); assert(delivered == 2);
 // Power-off while delivery is queued cannot deliver an old sample.
 acquire(); s_active = false; deliver(); assert(delivered == 2);
 unsigned old = wakes; prv_timer_callback(NULL); assert(wakes == old);
 s_active = true; s_startup_us = now_ms * 1000;
 acquire(); deliver(); assert(delivered == 2); // Startup transient suppressed.
 now_ms += 100; acquire(); deliver(); assert(delivered == 3);
 return 0;
}
"""


class QMI8658PollingTest(unittest.TestCase):
    def test_bounded_worker_pipeline(self):
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
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory), "-I", str(ROOT), str(harness),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
