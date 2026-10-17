/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif
#include "pbl/os/assert.h"
#include "pbl/os/malloc.h"
#include "pbl/os/mutex.h"
#include "pbl/os/tick.h"
#include "tal_api.h"
#include "tkl_output.h"

/* Privileged kernel primitives only. This does not provide app MPU isolation. */
typedef struct {
  SemaphoreHandle_t semaphore;
  uint32_t depth;
  bool recursive;
} MutexState;
struct pebble_mutex_t {
  MutexState state;
};
struct pebble_recursive_mutex_t {
  MutexState state;
};

static void prv_task_only(void) {
  uint32_t ipsr;
  __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
  OS_ASSERT(ipsr == 0);
}

/* The pinned SDK exports ABI-compatible os_malloc/os_free from mem_arch.c.
 * Use that pair directly; defining wrappers here would collide at link time. */
void *os_malloc_check(size_t size) {
  void *ptr = os_malloc(size);
  OS_ASSERT(ptr != NULL);
  return ptr;
}

void os_log(const char *file, int line, const char *message) {
  tkl_log_output("%s:%d %s\r\n", file, line, message);
}

NORETURN os_assertion_failed_lr(const char *file, int line, uint32_t lr) {
  tkl_log_output("Pebble OS assertion: %s:%d LR=%08lx\r\n", file, line, (unsigned long)lr);
  /* Fail closed. Returning would resume code with a broken locking invariant. */
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

NORETURN os_assertion_failed(const char *file, int line) {
  os_assertion_failed_lr(file, line, (uint32_t)__builtin_return_address(0));
}

TickType_t milliseconds_to_ticks(uint32_t ms) {
  return (TickType_t)((uint64_t)ms * configTICK_RATE_HZ / 1000);
}

uint32_t ticks_to_milliseconds(TickType_t ticks) {
  return (uint64_t)ticks * 1000 / configTICK_RATE_HZ;
}

static TickType_t prv_timeout(uint32_t ms) {
  uint64_t ticks = ((uint64_t)ms * configTICK_RATE_HZ + 999) / 1000;
  return ticks >= portMAX_DELAY ? portMAX_DELAY - 1 : (TickType_t)ticks;
}

static MutexState *prv_create(bool recursive) {
  prv_task_only();
  MutexState *s = os_malloc(sizeof(*s));
  if (!s) {
    return NULL;
  }
  s->semaphore = recursive ? xSemaphoreCreateRecursiveMutex() : xSemaphoreCreateMutex();
  if (!s->semaphore) {
    os_free(s);
    return NULL;
  }
  s->recursive = recursive;
  s->depth = 0;
  return s;
}

static bool prv_take(MutexState *s, TickType_t ticks) {
  prv_task_only();
  OS_ASSERT(s && s->semaphore);
  BaseType_t ok = s->recursive ? xSemaphoreTakeRecursive(s->semaphore, ticks)
                               : xSemaphoreTake(s->semaphore, ticks);
  if (ok != pdTRUE) {
    return false;
  }
  ++s->depth;
  return true;
}

static void prv_give(MutexState *s) {
  prv_task_only();
  OS_ASSERT(s && s->depth && xSemaphoreGetMutexHolder(s->semaphore) == xTaskGetCurrentTaskHandle());
  --s->depth;
  BaseType_t ok =
      s->recursive ? xSemaphoreGiveRecursive(s->semaphore) : xSemaphoreGive(s->semaphore);
  OS_ASSERT(ok == pdTRUE);
}

PebbleMutex *mutex_create(void) {
  return (PebbleMutex *)prv_create(false);
}
PebbleRecursiveMutex *mutex_create_recursive(void) {
  return (PebbleRecursiveMutex *)prv_create(true);
}
void mutex_destroy(PebbleMutex *mutex) {
  prv_task_only();
  OS_ASSERT(mutex && mutex->state.depth == 0);
  vSemaphoreDelete(mutex->state.semaphore);
  os_free(mutex);
}
void mutex_lock(PebbleMutex *mutex) {
  OS_ASSERT(prv_take((MutexState *)mutex, portMAX_DELAY));
}
void mutex_lock_with_lr(PebbleMutex *mutex, uint32_t lr) {
  OS_ASSERT_LR(prv_take((MutexState *)mutex, portMAX_DELAY), lr);
}
bool mutex_lock_with_timeout(PebbleMutex *mutex, uint32_t ms) {
  return prv_take((MutexState *)mutex, prv_timeout(ms));
}
void mutex_unlock(PebbleMutex *mutex) {
  prv_give((MutexState *)mutex);
}
void mutex_lock_recursive(PebbleRecursiveMutex *mutex) {
  OS_ASSERT(prv_take((MutexState *)mutex, portMAX_DELAY));
}
bool mutex_lock_recursive_with_timeout(PebbleRecursiveMutex *mutex, uint32_t ms) {
  return prv_take((MutexState *)mutex, prv_timeout(ms));
}
bool mutex_lock_recursive_with_timeout_and_lr(PebbleRecursiveMutex *mutex, uint32_t ms,
                                              uint32_t lr) {
  (void)lr;
  return mutex_lock_recursive_with_timeout(mutex, ms);
}
void mutex_unlock_recursive(PebbleRecursiveMutex *mutex) {
  prv_give((MutexState *)mutex);
}
bool mutex_is_owned_recursive(PebbleRecursiveMutex *mutex) {
  prv_task_only();
  OS_ASSERT(mutex);
  return xSemaphoreGetMutexHolder(mutex->state.semaphore) == xTaskGetCurrentTaskHandle();
}
void mutex_assert_held_by_curr_task(PebbleMutex *mutex, bool held) {
  prv_task_only();
  OS_ASSERT(mutex);
  OS_ASSERT((xSemaphoreGetMutexHolder(mutex->state.semaphore) == xTaskGetCurrentTaskHandle()) ==
            held);
}
void mutex_assert_recursive_held_by_curr_task(PebbleRecursiveMutex *mutex, bool held) {
  mutex_assert_held_by_curr_task((PebbleMutex *)mutex, held);
}

void tuya_os_selftest(void) {
  PebbleMutex *m = mutex_create();
  OS_ASSERT(m);
  mutex_lock(m);
  mutex_assert_held_by_curr_task(m, true);
  OS_ASSERT(!mutex_lock_with_timeout(m, 0));
  mutex_unlock(m);
  mutex_destroy(m);
  PebbleRecursiveMutex *r = mutex_create_recursive();
  OS_ASSERT(r);
  mutex_lock_recursive(r);
  OS_ASSERT(mutex_lock_recursive_with_timeout(r, 0));
  mutex_unlock_recursive(r);
  OS_ASSERT(mutex_is_owned_recursive(r));
  mutex_unlock_recursive(r);
  mutex_assert_recursive_held_by_curr_task(r, false);
  mutex_destroy((PebbleMutex *)r);
}
