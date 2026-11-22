/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/kernel.h>
#include <pbl/soc/t5ai/native_alarm.h>

static PBL_MUTEX_DEFINE(s_alarm_mutex);
static uint32_t s_generation;
static uint32_t s_wake_tick;
static bool s_initialized;
static bool s_active;
static bool s_fired;
static bool s_resumed;
static bool s_faulted;

static status_t prv_exchange(T5AIAlarmOperation operation, uint32_t generation, uint32_t ticks,
                             uint32_t *applied) {
  uint32_t abi = 0u;
  const status_t status =
      t5ai_cp_mailbox_alarm_request(operation, generation, ticks, applied, &abi);
  if (status != S_SUCCESS || abi != T5AI_PM_NATIVE_ALARM_ABI ||
      ((operation == T5AIAlarm_Cancel || operation == T5AIAlarm_FinishCleanup) &&
       *applied != generation)) {
    pbl_irq_lock();
    s_faulted = true;
    s_active = false;
    pbl_irq_unlock();
    return status != S_SUCCESS ? status : E_ERROR;
  }
  return S_SUCCESS;
}

status_t t5ai_native_alarm_init(void) {
  pbl_mutex_lock(&s_alarm_mutex, PBL_FOREVER);
  if (s_initialized || s_faulted) {
    const status_t status = s_faulted ? E_INVALID_OPERATION : S_SUCCESS;
    pbl_mutex_unlock(&s_alarm_mutex);
    return status;
  }
  uint32_t generation = 0u;
  status_t status = prv_exchange(T5AIAlarm_Query, 0u, 0u, &generation);
  // CP can survive a warm AP restart. Retire only this owner's previous alarm.
  if (status == S_SUCCESS && generation != 0u) {
    uint32_t applied;
    status = prv_exchange(T5AIAlarm_Cancel, generation, 0u, &applied);
    if (status == S_SUCCESS)
      status = prv_exchange(T5AIAlarm_FinishCleanup, generation, 0u, &applied);
  }
  pbl_irq_lock();
  s_generation = generation;
  s_initialized = status == S_SUCCESS;
  pbl_irq_unlock();
  pbl_mutex_unlock(&s_alarm_mutex);
  return status;
}

static status_t prv_arm(T5AIAlarmOperation operation, uint32_t ticks, uint32_t *generation) {
  if (generation == NULL || ticks < T5AI_PM_NATIVE_ALARM_MIN_TICKS ||
      ticks > T5AI_PM_NATIVE_ALARM_MAX_TICKS)
    return E_INVALID_ARGUMENT;
  pbl_mutex_lock(&s_alarm_mutex, PBL_FOREVER);
  pbl_irq_lock();
  if (!s_initialized || s_faulted || s_active || s_generation == UINT32_MAX) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&s_alarm_mutex);
    return E_INVALID_OPERATION;
  }
  *generation = ++s_generation;
  s_active = true;
  s_fired = false;
  s_resumed = false;
  pbl_irq_unlock();
  uint32_t applied;
  status_t status = prv_exchange(operation, *generation, ticks, &applied);
  if (status == S_SUCCESS && applied != *generation) {
    pbl_irq_lock();
    s_faulted = true;
    s_active = false;
    pbl_irq_unlock();
    status = E_ERROR;
  }
  pbl_mutex_unlock(&s_alarm_mutex);
  return status;
}

status_t t5ai_native_alarm_arm(uint32_t ticks, uint32_t *generation) {
  return prv_arm(T5AIAlarm_Arm, ticks, generation);
}

status_t t5ai_native_alarm_prepare_sleep(uint32_t ticks, uint32_t *generation) {
#ifdef CONFIG_T5AI_RETAINED_SLEEP_WAKE_ONLY
  // AP-only policy takes precedence over every CP transition diagnostic.
  return prv_arm(T5AIAlarm_ArmSleepWakeOnly, ticks, generation);
#elif !defined(CONFIG_T5AI_RETAINED_SLEEP_TEST)
  // A missing production policy must never select a CP low-voltage experiment.
  return E_INVALID_OPERATION;
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_ONLY_CONTROL)
  return prv_arm(T5AIAlarm_ArmSleepMaskOnly, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_MASK_BARRIER_CONTROL)
  return prv_arm(T5AIAlarm_ArmSleepMaskBarrier, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_SHALLOW_WFI_CONTROL)
  return prv_arm(T5AIAlarm_ArmSleepAnalogShallowWFI, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_ANALOG_DOMAINS_RETAINED)
  return prv_arm(T5AIAlarm_ArmSleepAnalogRetainDomains, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_ANALOG_WAKE_TEST)
  return prv_arm(T5AIAlarm_ArmSleepAnalogWake, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_PERSISTENT_TRACE)
  return prv_arm(T5AIAlarm_ArmSleepPersistentTrace, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_VENDOR_LPO_HALT)
  return prv_arm(T5AIAlarm_ArmSleepVendorLPOHalt, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_MASKED_CP_WFI)
  return prv_arm(T5AIAlarm_ArmSleepMaskedCPWFIKeepHF, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_VENDOR_MASKED)
#ifdef CONFIG_T5AI_RETAINED_SLEEP_VENDOR_KEEP_HF
  return prv_arm(T5AIAlarm_ArmSleepVendorMaskedKeepHF, ticks, generation);
#else
  return prv_arm(T5AIAlarm_ArmSleepVendorMasked, ticks, generation);
#endif
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_VENDOR_SUBCORES_ON)
#ifdef CONFIG_T5AI_RETAINED_SLEEP_VENDOR_KEEP_HF
  return prv_arm(T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF, ticks, generation);
#else
  return prv_arm(T5AIAlarm_ArmSleepVendorSubcoresOn, ticks, generation);
#endif
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_VENDOR_CPU_IDLE)
  return prv_arm(T5AIAlarm_ArmSleepVendorCPUIdle, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_VENDOR_FLASH_IDLE)
  return prv_arm(T5AIAlarm_ArmSleepVendorFlashIdle, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_NO_HW_MASK)
#ifdef CONFIG_T5AI_RETAINED_SLEEP_WFI_KEEP_HF
  return prv_arm(T5AIAlarm_ArmSleepNoHardwareMask, ticks, generation);
#else
  return prv_arm(T5AIAlarm_ArmSleepNoHardwareMaskClocksOff, ticks, generation);
#endif
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_WFI_KEEP_HF)
  return prv_arm(T5AIAlarm_ArmSleepKeepHF, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_RESOURCES_ONLY)
  return prv_arm(T5AIAlarm_ArmSleepResourcesOnly, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_RETAIN_MODULE_STATE)
  return prv_arm(T5AIAlarm_ArmSleepRetainModules, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_DOMAIN_BYPASS)
  return prv_arm(T5AIAlarm_ArmSleepDomainBypass, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_BSP_RESTORE_ONLY)
  return prv_arm(T5AIAlarm_ArmSleepBspRestore, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_FLASH_RESTORE_ONLY)
  return prv_arm(T5AIAlarm_ArmSleepFlashRestore, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_CORE_ONLY)
  return prv_arm(T5AIAlarm_ArmSleepCoreOnly, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_KEEP_HF)
  return prv_arm(T5AIAlarm_ArmSleepClocksLive, ticks, generation);
#elif defined(CONFIG_T5AI_RETAINED_SLEEP_CLOCKS_ONLY)
  return prv_arm(T5AIAlarm_ArmSleepClocksOnly, ticks, generation);
#else
  return prv_arm(T5AIAlarm_ArmSleep, ticks, generation);
#endif
}

status_t t5ai_native_alarm_cp_query(T5AIAlarmOperation operation, uint32_t *value) {
  if (value == NULL ||
      (operation != T5AIAlarm_State && operation != T5AIAlarm_AnalogWakeState &&
       operation != T5AIAlarm_MaskBarrierCount && operation != T5AIAlarm_CleanupIdleRefusals &&
       operation != T5AIAlarm_CleanupIRQState && operation != T5AIAlarm_CleanupIRQKicks &&
       operation != T5AIAlarm_CleanupIRQDelivery && operation != T5AIAlarm_CleanupMailboxISRs &&
       operation != T5AIAlarm_CleanupMailboxEmptyStatus &&
       operation != T5AIAlarm_CleanupMailboxProgress && operation != T5AIAlarm_NormalSleepSkips &&
       !(operation >= T5AIAlarm_PMUPolicyBefore && operation <= T5AIAlarm_PeripheralClocksAfter) &&
       operation != T5AIAlarm_AnalogPrepareLeadTicks &&
       operation != T5AIAlarm_AnalogPrepareReject && operation != T5AIAlarm_LowVoltageCount &&
       operation != T5AIAlarm_MissingVotesLow && operation != T5AIAlarm_MissingVotesHigh &&
       operation != T5AIAlarm_ReadySamples && operation != T5AIAlarm_Stage &&
       operation != T5AIAlarm_PreviousStage && operation != T5AIAlarm_PreviousGeneration &&
       operation != T5AIAlarm_ResourceCount &&
       !(operation >= T5AIAlarm_ClockCount && operation <= T5AIAlarm_FinalNvicHigh) &&
       !(operation >= T5AIAlarm_FinalReject && operation <= T5AIAlarm_MaskAtFinal) &&
       !(operation >= T5AIAlarm_HalReject && operation <= T5AIAlarm_LowVoltageTicks) &&
       !(operation >= T5AIAlarm_FinalAPPendingLow && operation <= T5AIAlarm_FinalGPIOStatusHigh) &&
       !(operation >= T5AIAlarm_PowerParameter && operation <= T5AIAlarm_WakeStatusAfter) &&
       !(operation >= T5AIAlarm_PMUWakeCount && operation <= T5AIAlarm_HardwareMaskAfterWFI) &&
       !(operation >= T5AIAlarm_BootPMUWake && operation <= T5AIAlarm_BootPMUSnapshotValid)))
    return E_INVALID_ARGUMENT;
  pbl_mutex_lock(&s_alarm_mutex, PBL_FOREVER);
  const status_t status = prv_exchange(operation, 0u, 0u, value);
  pbl_mutex_unlock(&s_alarm_mutex);
  return status;
}

status_t t5ai_native_alarm_cancel(uint32_t generation) {
  pbl_mutex_lock(&s_alarm_mutex, PBL_FOREVER);
  pbl_irq_lock();
  if (!s_initialized || generation == 0u || generation != s_generation) {
    pbl_irq_unlock();
    pbl_mutex_unlock(&s_alarm_mutex);
    return E_INVALID_ARGUMENT;
  }
  s_active = false;
  pbl_irq_unlock();
  uint32_t applied;
  const status_t status = prv_exchange(T5AIAlarm_Cancel, generation, 0u, &applied);
  pbl_mutex_unlock(&s_alarm_mutex);
  return status;
}

status_t t5ai_native_alarm_finish_cleanup(uint32_t generation) {
  pbl_mutex_lock(&s_alarm_mutex, PBL_FOREVER);
  pbl_irq_lock();
  const bool valid = s_initialized && !s_active && generation != 0u && generation == s_generation;
  pbl_irq_unlock();
  if (!valid) {
    pbl_mutex_unlock(&s_alarm_mutex);
    return E_INVALID_ARGUMENT;
  }
  uint32_t applied;
  const status_t status = prv_exchange(T5AIAlarm_FinishCleanup, generation, 0u, &applied);
  pbl_mutex_unlock(&s_alarm_mutex);
  return status;
}

void t5ai_native_alarm_notify(uint32_t generation, uint32_t tick, uint32_t abi) {
  pbl_irq_lock();
  if (abi != T5AI_PM_NATIVE_ALARM_ABI || generation > s_generation) {
    // Ignore stale generations, but a corrupt/future event invalidates the session.
    if (s_initialized)
      s_faulted = true;
  } else if (s_active && generation == s_generation && !s_fired) {
    s_wake_tick = tick;
    s_fired = true;
  }
  pbl_irq_unlock();
}

bool t5ai_native_alarm_fired(uint32_t generation, uint32_t *tick) {
  pbl_irq_lock();
  const bool fired = !s_faulted && s_fired && generation == s_generation;
  if (fired && tick != NULL)
    *tick = s_wake_tick;
  pbl_irq_unlock();
  return fired;
}

bool t5ai_native_alarm_healthy(void) {
  pbl_irq_lock();
  const bool healthy = s_initialized && !s_faulted;
  pbl_irq_unlock();
  return healthy;
}

void t5ai_native_alarm_resume_notify(uint32_t generation, uint32_t tick, uint32_t abi) {
  pbl_irq_lock();
  if (abi != T5AI_PM_NATIVE_ALARM_ABI || generation > s_generation) {
    if (s_initialized)
      s_faulted = true;
  } else if (s_active && generation == s_generation) {
    s_wake_tick = tick;
    s_resumed = true;
  }
  pbl_irq_unlock();
}

bool t5ai_native_alarm_woken(uint32_t generation) {
  pbl_irq_lock();
  const bool woken = s_faulted || generation != s_generation || s_fired || s_resumed || !s_active;
  pbl_irq_unlock();
  return woken;
}

bool t5ai_native_alarm_event_received(uint32_t generation) {
  pbl_irq_lock();
  const bool received =
      !s_faulted && s_active && generation == s_generation && (s_fired || s_resumed);
  pbl_irq_unlock();
  return received;
}
