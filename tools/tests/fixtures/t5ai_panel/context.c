/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdlib.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>

// Scheduler execution is modeled; sleep conversion is the complete kernel/time.c.
static bool kernel_started, in_isr, irq_locked, sched_locked;
static unsigned sleeps;
static void (*panic_observer)(void);
static void (*sleep_observer)(pbl_timeout_t);
bool pbl_kernel_is_started(void) {
  return kernel_started;
}
bool pbl_kernel_is_running(void) {
  return kernel_started && !sched_locked;
}
bool pbl_in_isr(void) {
  return in_isr;
}
bool pbl_irq_is_locked(void) {
  return irq_locked;
}
void pbl_thread_sleep(pbl_timeout_t timeout) {
  assert(kernel_started && !in_isr && !irq_locked && !sched_locked);
  ++sleeps;
  assert(sleep_observer);
  sleep_observer(timeout);
}
void pbl_thread_yield(void) {
  abort();
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  if (panic_observer)
    panic_observer();
  abort();
}
[[noreturn]] void pbl_panic_bare(void) {
  if (panic_observer)
    panic_observer();
  abort();
}
