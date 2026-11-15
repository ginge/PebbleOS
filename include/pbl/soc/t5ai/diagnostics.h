/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/thread.h>
#include <pbl/kernel/work.h>

// Diagnostic callers hold the kernel IRQ lock while walking held mutexes.
static inline unsigned t5ai_diagnostics_held_mutexes(const struct pbl_thread *thread) {
  unsigned count = 0;
  for (const struct pbl_mutex *m = thread->backend.held; m; m = m->backend.next_held)
    ++count;
  return count;
}
