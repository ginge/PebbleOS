/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sched.h>
#include <pbl/kernel/thread.h>
#include <pbl/soc/t5ai/mailbox_channel.h>

#include <kernel/pebble_tasks.h>
#include <logging/logging_private.h>

// T5AI reserves TLS slot 0 for kernel logging; no thread/retained layout change.
#define LOG_ENTERED_TLS_SLOT 0
static_assert(CONFIG_KERNEL_THREAD_TLS_SLOTS > LOG_ENTERED_TLS_SLOT);

static PBL_MUTEX_DEFINE(s_log_mutex);
static LogState s_log_state;
static bool s_log_ready, s_early_entered, s_log_mutex_held;

// The stock initialization still owns all non-logging applib state.
void t5ai_stock_kernel_applib_init(void);

static bool prv_enter(struct pbl_thread *thread) {
  pbl_irq_lock();
  const bool entered =
      thread ? pbl_thread_tls_get(thread, LOG_ENTERED_TLS_SLOT) != NULL : s_early_entered;
  if (!entered) {
    if (thread) {
      pbl_thread_tls_set(thread, LOG_ENTERED_TLS_SLOT, &s_log_state);
    } else {
      s_early_entered = true;
    }
  }
  pbl_irq_unlock();
  return !entered;
}

static void prv_leave(struct pbl_thread *thread) {
  pbl_irq_lock();
  if (thread) {
    pbl_thread_tls_set(thread, LOG_ENTERED_TLS_SLOT, NULL);
  } else {
    s_early_entered = false;
  }
  pbl_irq_unlock();
}

static LogState *prv_get_log_state(void) {
  struct pbl_thread *thread = pbl_kernel_is_started() ? pbl_thread_current() : NULL;
  if (!prv_enter(thread)) {
    return NULL;
  }

  // Preserve early/interrupt logging and the no-logging window before applib init.
  const bool use_mutex = pbl_kernel_is_running() &&
                         pebble_task_get_thread(PebbleTask_KernelMain) != NULL && !pbl_in_isr() &&
                         !pbl_irq_is_locked();
  if (use_mutex && (!s_log_ready || pbl_mutex_lock(&s_log_mutex, PBL_MSEC(1000)) != 0)) {
    prv_leave(thread);
    return NULL;
  }

  pbl_irq_lock();
  if (s_log_state.in_progress) {
    pbl_irq_unlock();
    if (use_mutex) {
      pbl_mutex_unlock(&s_log_mutex);
    }
    prv_leave(thread);
    return NULL;
  }
  s_log_state.in_progress = true;
  s_log_mutex_held = use_mutex;
  pbl_irq_unlock();
  return &s_log_state;
}

LogState *kernel_applib_get_log_state(void) {
  // Reject before the log-state mutex, and therefore before PULSE locks.
  if (t5ai_mailbox_channel_is_owner()) {
    return NULL;
  }
  return prv_get_log_state();
}

void kernel_applib_release_log_state(LogState *state) {
  struct pbl_thread *thread = pbl_kernel_is_started() ? pbl_thread_current() : NULL;
  pbl_irq_lock();
  state->in_progress = false;
  const bool held = s_log_mutex_held;
  s_log_mutex_held = false;
  pbl_irq_unlock();
  if (held) {
    pbl_mutex_unlock(&s_log_mutex);
  }
  prv_leave(thread);
}

void kernel_applib_init(void) {
  s_log_ready = true;
  t5ai_stock_kernel_applib_init();
}
