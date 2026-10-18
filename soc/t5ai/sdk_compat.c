/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kernel/memory_layout.h"
#include "kernel/pebble_tasks.h"
#include "process_management/app_manager.h"
#include "process_management/worker_manager.h"
#include "process_management/process_manager.h"
#include "process_management/pebble_process_md.h"
#include <pbl/kernel/thread.h>

#include <stdbool.h>
#include <stdint.h>

#define SDK_VALID (1u << 0)
#define SDK_LEGACY2 (1u << 1)
#define SDK_LEGACY3 (1u << 2)

// Apps may read these flags, but only privileged process lifecycle code writes them.
static volatile uint32_t s_app_sdk KERNEL_READONLY_DATA;
static volatile uint32_t s_worker_sdk KERNEL_READONLY_DATA;
extern struct pbl_thread *pbl_cur;
extern struct pbl_thread *g_task_threads[NumPebbleTask];

void __real_process_manager_process_setup(PebbleTask task);
void __real_process_manager_process_cleanup(PebbleTask task);
bool __real_process_manager_compiled_with_legacy2_sdk(void);
bool __real_process_manager_compiled_with_legacy3_sdk(void);

void __wrap_process_manager_process_setup(PebbleTask task) {
  __real_process_manager_process_setup(task);
  ProcessContext *context =
      task == PebbleTask_App ? app_manager_get_task_context() : worker_manager_get_task_context();
  const ProcessAppSDKType type = process_metadata_get_app_sdk_type(context->app_md);
  const uint32_t flags = SDK_VALID | (type == ProcessAppSDKType_Legacy2x ? SDK_LEGACY2 : 0u) |
                         (type == ProcessAppSDKType_Legacy3x ? SDK_LEGACY3 : 0u);
  // Setup completes before the new App/Worker thread is created.
  if (task == PebbleTask_App) {
    s_app_sdk = flags;
  } else if (task == PebbleTask_Worker) {
    s_worker_sdk = flags;
  }
}

void __wrap_process_manager_process_cleanup(PebbleTask task) {
  __real_process_manager_process_cleanup(task);
  // The old thread has been unregistered and aborted before invalidation.
  if (task == PebbleTask_App) {
    s_app_sdk = 0u;
  } else if (task == PebbleTask_Worker) {
    s_worker_sdk = 0u;
  }
}

static inline __attribute__((always_inline)) uint32_t prv_flags(void) {
  struct pbl_thread *current = pbl_cur;
  if (current) {
    if (current == g_task_threads[PebbleTask_App]) {
      return s_app_sdk;
    }
    if (current == g_task_threads[PebbleTask_Worker]) {
      return s_worker_sdk;
    }
  }
  // Stock compatibility queries return false for every other task.
  return SDK_VALID;
}

bool __wrap_process_manager_compiled_with_legacy2_sdk(void) {
  const uint32_t flags = prv_flags();
  return flags & SDK_VALID ? (flags & SDK_LEGACY2) != 0u
                           : __real_process_manager_compiled_with_legacy2_sdk();
}

bool __wrap_process_manager_compiled_with_legacy3_sdk(void) {
  const uint32_t flags = prv_flags();
  return flags & SDK_VALID ? (flags & SDK_LEGACY3) != 0u
                           : __real_process_manager_compiled_with_legacy3_sdk();
}
