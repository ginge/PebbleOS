/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>

#include <kernel/kernel_applib_state.h>
#include <kernel/pebble_tasks.h>
#include <pthread.h>

static _Thread_local struct pbl_thread self;
static _Thread_local unsigned irq_depth;
static _Thread_local bool isr, owner, fail_lock, recurse_lock, recurse_unlock;
static bool started, running, registered;
static atomic_uint attempts, unlocks, current_calls;
static unsigned service_inits;
static pthread_mutex_t irq = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool worker_done, worker_obtained, retry_allowed;
static struct pbl_thread main_thread;
static struct timespec deadline(void) {
  struct timespec value;
  assert(!clock_gettime(CLOCK_REALTIME, &value));
  value.tv_sec += 3;
  return value;
}
void pbl_irq_lock(void) {
  if (!irq_depth++)
    assert(!pthread_mutex_lock(&irq));
}
void pbl_irq_unlock(void) {
  assert(irq_depth);
  if (!--irq_depth)
    assert(!pthread_mutex_unlock(&irq));
}
bool pbl_irq_is_locked(void) {
  return irq_depth != 0;
}
bool pbl_in_isr(void) {
  return isr;
}
bool pbl_kernel_is_started(void) {
  return started;
}
bool pbl_kernel_is_running(void) {
  return running;
}
struct pbl_thread *pbl_thread_current(void) {
  assert(started);
  ++current_calls;
  return &self;
}
struct pbl_thread *pebble_task_get_thread(PebbleTask task) {
  assert(task == PebbleTask_KernelMain);
  return registered ? &main_thread : NULL;
}
PebbleTask pebble_task_get_current(void) {
  return PebbleTask_Unknown;
}
bool t5ai_mailbox_channel_is_owner(void) {
  return owner;
}
void connection_service_state_init(ConnectionServiceState *state) {
  ++service_inits;
}
void battery_state_service_state_init(BatteryStateServiceState *state) {
  ++service_inits;
}
void t5ai_stock_release_log_state(LogState *state);
void t5ai_stock_kernel_applib_init(void);

// Back each real kernel mutex with a native recursive mutex; keep its real owner/count.
static struct {
  struct pbl_mutex *object;
  pthread_mutex_t native;
} locks[4];
static pthread_mutex_t lock_map = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t *native_mutex(struct pbl_mutex *object) {
  assert(!pthread_mutex_lock(&lock_map));
  for (unsigned n = 0; n < 4; ++n) {
    if (!locks[n].object) {
      pthread_mutexattr_t attr;
      assert(!pthread_mutexattr_init(&attr));
      assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
      assert(!pthread_mutex_init(&locks[n].native, &attr));
      assert(!pthread_mutexattr_destroy(&attr));
      locks[n].object = object;
    }
    if (locks[n].object == object) {
      assert(!pthread_mutex_unlock(&lock_map));
      return &locks[n].native;
    }
  }
  abort();
}
int pbl_mutex_lock_lr(struct pbl_mutex *object, pbl_timeout_t timeout, uintptr_t lr) {
  assert(!irq_depth && !isr && running && !owner);
  assert(timeout.ticks == pbl_ms_to_ticks(1000));
  assert(!pthread_mutex_lock(&gate));
  ++attempts;
  assert(!pthread_cond_broadcast(&changed));
  assert(!pthread_mutex_unlock(&gate));
  if (recurse_lock) {
    recurse_lock = false;
    assert(!kernel_applib_get_log_state());
  }
  if (fail_lock)
    return -EAGAIN;
  struct timespec end;
  assert(!clock_gettime(CLOCK_REALTIME, &end));
  ++end.tv_sec;
  int result = pthread_mutex_timedlock(native_mutex(object), &end);
  if (result)
    return -EAGAIN;
  assert(!object->owner || object->owner == &self);
  object->owner = &self;
  ++object->count;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *object) {
  assert(object->owner == &self && object->count);
  if (recurse_unlock) {
    recurse_unlock = false;
    assert(!kernel_applib_get_log_state());
  }
  if (!--object->count)
    object->owner = NULL;
  ++unlocks;
  assert(!pthread_mutex_unlock(native_mutex(object)));
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  abort();
}

#include ADAPTER_SOURCE

static void setup(void) {
  started = running = registered = true;
  kernel_applib_init();
  assert(service_inits == 2);
}
static void *worker(void *timeout_then_retry) {
  LogState *state = kernel_applib_get_log_state();
  assert(!pthread_mutex_lock(&gate));
  worker_obtained = state != NULL;
  worker_done = true;
  assert(!pthread_cond_broadcast(&changed));
  if (timeout_then_retry) {
    assert(!state);
    struct timespec end = deadline();
    while (!retry_allowed)
      assert(!pthread_cond_timedwait(&changed, &gate, &end));
    assert(!pthread_mutex_unlock(&gate));
    state = kernel_applib_get_log_state();
    assert(state);
  } else {
    assert(!pthread_mutex_unlock(&gate));
  }
  if (state) {
    strcpy(state->buffer, "worker");
    assert(!kernel_applib_get_log_state());
    kernel_applib_release_log_state(state);
  }
  return NULL;
}
