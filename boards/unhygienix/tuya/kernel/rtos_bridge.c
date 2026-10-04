/* SPDX-License-Identifier: Apache-2.0 */
/* These entry points execute privileged; do not remap to vendor MPU wrappers. */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#include "rtos_bridge.h"
#include "task_memory.h"
#include "svc_bridge.h"
#include "pbl/mcu/syscall_abi.h"
#include <stdbool.h>
#include <stdint.h>

_Static_assert(configENABLE_MPU == 1, "Pebble tasks require MPU context switching");
_Static_assert(configENABLE_TRUSTZONE == 0, "TrustZone context switching is not ported");
_Static_assert(configNUM_THREAD_LOCAL_STORAGE_POINTERS >= PBL_SYSCALL_TLS_REQUIRED,
               "Vendor TLS slots must not alias Pebble syscall state");
_Static_assert(PBL_SYSCALL_TLS_BASE >= 3, "Keep vendor TLS slots 0..2 reserved");
_Static_assert(configPEBBLE_CORE_ID >= 0 && configPEBBLE_CORE_ID < configNUMBER_OF_CORES,
               "Invalid Pebble core affinity");

extern bool xApplicationIsAllowedToRaisePrivilege(uint32_t pc);
extern bool mcu_call_unprivileged_reentry_is_allowed(uint32_t pc);
extern uint32_t *xApplicationGetSyscallStack(uintptr_t *base);
extern void vSetupSyscallRegisters(uintptr_t original_sp, uintptr_t *lr);
extern const uint32_t __syscall_text_start__[], __syscall_text_end__[];

static bool prv_task_stack(void *context, TuyaSvcRange *range) {
  (void)context;
  uint32_t top, high, size;
  if (xTaskGetStackInfo(xTaskGetCurrentTaskHandle(), &top, &high, &size) != pdTRUE || size > high ||
      size == 0) {
    return false;
  }
  *range = (TuyaSvcRange){.low = high - size, .high = high};
  return true;
}
static bool prv_authorize(void *context, uint32_t pc) {
  (void)context;
  if (!xApplicationIsAllowedToRaisePrivilege(pc)) {
    return false;
  }
  if (mcu_call_unprivileged_reentry_is_allowed(pc)) {
    return true;
  }
  const uintptr_t low = (uintptr_t)__syscall_text_start__;
  const uintptr_t high = (uintptr_t)__syscall_text_end__;
  return pc >= low && pc - low >= 2 && pc < high;
}
static uint16_t prv_instruction(void *context, uint32_t address) {
  (void)context;
  return *(const volatile uint16_t *)(uintptr_t)address;
}
static bool prv_syscall_stack(void *context, TuyaSvcRange *range) {
  (void)context;
  uintptr_t base = 0;
  uint32_t *top = xApplicationGetSyscallStack(&base);
  *range = top ? (TuyaSvcRange){base, (uintptr_t)top} : (TuyaSvcRange){0};
  return true;
}
static void prv_materialize_fp(void *context) {
  (void)context;
#if configENABLE_FPU
  uint32_t fpscr;
  __asm volatile("vmrs %0, fpscr" : "=r"(fpscr)::"memory");
  (void)fpscr;
#endif
}
static void prv_setup_return(void *context, uintptr_t original_sp, uint32_t *lr) {
  (void)context;
  vSetupSyscallRegisters(original_sp, (uintptr_t *)lr);
}
static void prv_commit(void *context, uint32_t *frame, uintptr_t limit) {
  (void)context;
  if (limit) {
    __asm volatile("msr psplim, %0\n msr psp, %1\n isb" ::"r"(limit), "r"(frame) : "memory");
  }
  uint32_t control;
  __asm volatile("mrs %0, control" : "=r"(control));
  control &= ~1u;
  __asm volatile("msr control, %0\n isb" ::"r"(control) : "memory");
}

/* Called by the vendor SVC handler before it decodes its own SVC numbers. */
bool pebble_tuya_svc_dispatch(uint32_t *frame, uint32_t exc_return) {
  uint32_t control;
  __asm volatile("mrs %0, control" : "=r"(control));
  if (!(control & 1u)) {
    return false;
  }
  const uint32_t expected = configRUN_FREERTOS_SECURE_ONLY ? 0xfffffffdu : 0xffffffbcu;
  if ((exc_return | 0x10u) != expected) {
    __builtin_trap();
  }
#if !configENABLE_FPU
  if (!(exc_return & 0x10u)) {
    __builtin_trap();
  }
#endif
  static const TuyaSvcHooks hooks = {
      .task_stack = prv_task_stack,
      .authorize = prv_authorize,
      .instruction = prv_instruction,
      .syscall_stack = prv_syscall_stack,
      .materialize_fp = prv_materialize_fp,
      .setup_return = prv_setup_return,
      .commit = prv_commit,
  };
  TuyaSvcResult result = tuya_svc_dispatch(frame, exc_return, control, portGET_CORE_ID(),
                                           configPEBBLE_CORE_ID, &hooks);
  /* Until app-fault teardown is connected, reject by faulting, never elevate. */
  if (result != TuyaSvc_Entered) {
    __builtin_trap();
  }
  return true;
}

_Static_assert(portNUM_CONFIGURABLE_REGIONS == 3, "Re-audit vendor region count");
_Static_assert(tskMPU_REGION_PRIVILEGED_ONLY == TuyaMemory_Privileged, "MPU flag ABI");
_Static_assert(tskMPU_REGION_READ_ONLY == TuyaMemory_ReadOnly &&
                   tskMPU_REGION_READ_WRITE == TuyaMemory_ReadWrite &&
                   tskMPU_REGION_EXECUTE_NEVER == TuyaMemory_ExecuteNever &&
                   tskMPU_REGION_NORMAL_MEMORY == TuyaMemory_Normal,
               "MPU flag ABI");

bool pebble_tuya_set_task_memory(TaskParameters_t *params,
                                 const struct MpuRegion *const regions[3]) {
  if (!params || !regions) {
    return false;
  }
  TuyaTaskMemory converted[3];
  for (size_t i = 0; i < 3; ++i) {
    if (!tuya_task_memory_from_pebble(regions[i], &converted[i])) {
      return false;
    }
  }
  /* Commit only after every descriptor converted successfully. */
  for (size_t i = 0; i < 3; ++i) {
    params->xRegions[i] = (MemoryRegion_t){(void *)(uintptr_t)converted[i].base, converted[i].size,
                                           converted[i].flags};
  }
  return true;
}

BaseType_t pebble_tuya_create_restricted_task(const TaskParameters_t *params,
                                              TaskHandle_t *handle) {
  if (!params || !params->pvTaskCode || !params->puxStackBuffer || !params->usStackDepth ||
      params->usStackDepth > UINT32_MAX / sizeof(StackType_t)) {
    return pdFAIL;
  }
  TuyaTaskMemory regions[3];
  for (size_t i = 0; i < 3; ++i) {
    regions[i] =
        (TuyaTaskMemory){(uint32_t)params->xRegions[i].pvBaseAddress,
                         params->xRegions[i].ulLengthInBytes, params->xRegions[i].ulParameters};
  }
  if (!tuya_task_memory_validate(regions, 3, (uint32_t)params->puxStackBuffer,
                                 params->usStackDepth * sizeof(StackType_t),
                                 (params->uxPriority & portPRIVILEGE_BIT) != 0)) {
    return pdFAIL;
  }
  return xTaskCreateRestrictedPinnedToCore(params, handle, configPEBBLE_CORE_ID);
}
