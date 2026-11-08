/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

// Board-owned scalar protocol on the existing AP/CP power channel.
#define T5AI_PM_NATIVE_ALARM_COMMAND   0x70u
#define T5AI_PM_NATIVE_SLEEP_RESUMED   0x72u
#define T5AI_PM_NATIVE_ALARM_WAKE      0x71u
#define T5AI_PM_NATIVE_ALARM_ABI       0x50424167u
#define T5AI_PM_NATIVE_ALARM_MIN_TICKS 64u
#define T5AI_PM_NATIVE_ALARM_MAX_TICKS 16000u
#define T5AI_SLEEP_CP_MIN_LEAD_TICKS   64u
#define T5AI_SLEEP_FINAL_REJECT_LEAD   (1u << 0)
#define T5AI_SLEEP_FINAL_REJECT_IRQ    (1u << 1)
#define T5AI_SLEEP_FINAL_REJECT_TICK   (1u << 2)
#define T5AI_SLEEP_FINAL_REJECT_MASK   (1u << 3)
#define T5AI_SLEEP_FINAL_REJECT_AP_IRQ (1u << 4)
#define T5AI_SLEEP_FINAL_REJECT_UART   (1u << 5)
#define T5AI_SLEEP_FINAL_REJECT_FLASH  (1u << 6)
#define T5AI_SLEEP_FINAL_REJECT_ARCH   (1u << 7)

typedef enum {
  T5AIAlarm_Query = 0,
  T5AIAlarm_Arm = 1,
  T5AIAlarm_Cancel = 2,
  T5AIAlarm_ArmSleep = 3,
  T5AIAlarm_State = 4,
  T5AIAlarm_LowVoltageCount = 5,
  T5AIAlarm_MissingVotesLow = 6,
  T5AIAlarm_MissingVotesHigh = 7,
  T5AIAlarm_ReadySamples = 8,
  T5AIAlarm_Stage = 9,
  T5AIAlarm_PreviousStage = 10,
  T5AIAlarm_PreviousGeneration = 11,
  T5AIAlarm_HalReject = 12,
  T5AIAlarm_HalPendingLow = 13,
  T5AIAlarm_HalPendingHigh = 14,
  T5AIAlarm_HalStatusLow = 15,
  T5AIAlarm_HalStatusHigh = 16,
  T5AIAlarm_PrepareTicks = 17,
  T5AIAlarm_LowVoltageTicks = 18,
  T5AIAlarm_ArmSleepWakeOnly = 19,
  T5AIAlarm_ArmSleepResourcesOnly = 20,
  T5AIAlarm_ResourceCount = 21,
  T5AIAlarm_ArmSleepClocksOnly = 22,
  T5AIAlarm_ClockCount = 23,
  T5AIAlarm_FinalLeadTicks = 24,
  T5AIAlarm_SleepConfig = 25,
  T5AIAlarm_WakeConfig = 26,
  T5AIAlarm_ClockModeLow = 27,
  T5AIAlarm_HighFrequencyLow = 28,
  T5AIAlarm_FinalPendingLow = 29,
  T5AIAlarm_FinalPendingHigh = 30,
  T5AIAlarm_FinalNvicLow = 31,
  T5AIAlarm_FinalNvicHigh = 32,
  T5AIAlarm_ArmSleepClocksLive = 33,
  T5AIAlarm_ArmSleepCoreOnly = 34,
  T5AIAlarm_FinalReject = 35,
  T5AIAlarm_FinalSkips = 36,
  T5AIAlarm_MaskBefore = 37,
  T5AIAlarm_MaskAfter = 38,
  T5AIAlarm_MaskAtFinal = 39,
  T5AIAlarm_ArmSleepFlashRestore = 40,
  T5AIAlarm_ArmSleepBspRestore = 41,
  T5AIAlarm_ArmSleepDomainBypass = 42,
  T5AIAlarm_ArmSleepRetainModules = 43,
  T5AIAlarm_FinalAPPendingLow = 44,
  T5AIAlarm_FinalAPPendingHigh = 45,
  T5AIAlarm_FinalGPIOStatusLow = 46,
  T5AIAlarm_FinalGPIOStatusHigh = 47,
  T5AIAlarm_ArmSleepKeepHF = 48,
  T5AIAlarm_ArmSleepNoHardwareMask = 49,
  T5AIAlarm_ArmSleepNoHardwareMaskClocksOff = 50,
  T5AIAlarm_PowerParameter = 51,
  T5AIAlarm_WakeStatusBefore = 52,
  T5AIAlarm_WakeStatusAfter = 53,
  T5AIAlarm_ArmSleepVendorFlashIdle = 54,
  T5AIAlarm_ArmSleepVendorCPUIdle = 55,
  T5AIAlarm_ArmSleepVendorSubcoresOn = 56,
  T5AIAlarm_ArmSleepVendorMasked = 57,
  T5AIAlarm_PMUWakeCount = 58,
  T5AIAlarm_PMUWakeSources = 59,
  T5AIAlarm_PMUWakeTicks = 60,
  T5AIAlarm_PMUAmbiguousCount = 61,
  T5AIAlarm_WakeClearBefore = 62,
  T5AIAlarm_WakeClearAfter = 63,
  T5AIAlarm_PMUClearHeldCount = 64,
  T5AIAlarm_HardwareMaskAfterWFI = 65,
  T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF = 66,
  T5AIAlarm_ArmSleepVendorMaskedKeepHF = 67,
  T5AIAlarm_ArmSleepMaskedCPWFIKeepHF = 68,
  T5AIAlarm_BootPMUWake = 69,
  T5AIAlarm_BootPMUReset = 70,
  T5AIAlarm_BootPMUPower = 71,
  T5AIAlarm_BootPMUWakeConfig = 72,
  T5AIAlarm_BootPMUSnapshotValid = 73,
  T5AIAlarm_ArmSleepVendorLPOHalt = 74,
  T5AIAlarm_ArmSleepPersistentTrace = 75,
  T5AIAlarm_AnalogWakeState = 76,
  T5AIAlarm_ArmSleepAnalogWake = 77,
  T5AIAlarm_AnalogPrepareLeadTicks = 78,
  T5AIAlarm_AnalogPrepareReject = 79,
  T5AIAlarm_ArmSleepAnalogRetainDomains = 80,
  T5AIAlarm_ArmSleepAnalogShallowWFI = 81,
  T5AIAlarm_ArmSleepMaskBarrier = 82,
  T5AIAlarm_MaskBarrierCount = 83,
  T5AIAlarm_CleanupIdleRefusals = 84,
  T5AIAlarm_CleanupIRQState = 85,
  T5AIAlarm_CleanupIRQKicks = 86,
  T5AIAlarm_CleanupIRQDelivery = 87,
  T5AIAlarm_CleanupMailboxISRs = 88,
  T5AIAlarm_CleanupMailboxEmptyStatus = 89,
  T5AIAlarm_CleanupMailboxProgress = 90,
  T5AIAlarm_FinishCleanup = 91,
  T5AIAlarm_NormalSleepSkips = 92,
  T5AIAlarm_PMUPolicyBefore = 93,
  T5AIAlarm_PMUPolicyAfter = 94,
  T5AIAlarm_IRQEnableLowBefore = 95,
  T5AIAlarm_IRQEnableLowAfter = 96,
  T5AIAlarm_IRQEnableHighBefore = 97,
  T5AIAlarm_IRQEnableHighAfter = 98,
  T5AIAlarm_PeripheralClocksBefore = 99,
  T5AIAlarm_PeripheralClocksAfter = 100,
  T5AIAlarm_ArmSleepMaskOnly = 101,
} T5AIAlarmOperation;

#define T5AI_SLEEP_STATE_SIBLING_RESET_REQUEST (1u << 0)
#define T5AI_SLEEP_STATE_SIBLING_POWER_DOWN    (1u << 1)
#define T5AI_SLEEP_STATE_SIBLING_RESET_ACK     (1u << 2)
#define T5AI_SLEEP_STATE_MEMORY_RETAINED       (1u << 3)
#define T5AI_SLEEP_STATE_PREPARED              (1u << 4)
#define T5AI_SLEEP_STATE_AP_PUBLISHED          (1u << 5)
#define T5AI_SLEEP_STATE_LOWVOLT_ENTERED       (1u << 6)
#define T5AI_SLEEP_STATE_SIBLING_POWER_ACK     (1u << 7)
#define T5AI_SLEEP_STATE_WATCHDOG_GUARD        (1u << 8)
#define T5AI_SLEEP_STATE_GPIO_RETAINED         (1u << 9)
#define T5AI_SLEEP_STATE_PERIPHERALS_RETAINED  (1u << 10)
#define T5AI_SLEEP_DOMAIN_BAKP_POWER_DOWN      (1u << 4)
#define T5AI_SLEEP_DOMAIN_AHBP_POWER_DOWN      (1u << 5)
#define T5AI_SLEEP_AP_RETAINED_DOMAINS \
  (T5AI_SLEEP_DOMAIN_BAKP_POWER_DOWN | T5AI_SLEEP_DOMAIN_AHBP_POWER_DOWN)
#define T5AI_SLEEP_REQUIRED_STATE                                     \
  (T5AI_SLEEP_SIBLING_OFF_MASK | T5AI_SLEEP_STATE_MEMORY_RETAINED |   \
   T5AI_SLEEP_STATE_WATCHDOG_GUARD | T5AI_SLEEP_STATE_GPIO_RETAINED | \
   T5AI_SLEEP_STATE_PERIPHERALS_RETAINED)
#define T5AI_SLEEP_SIBLING_OFF_MASK                                               \
  (T5AI_SLEEP_STATE_SIBLING_RESET_REQUEST | T5AI_SLEEP_STATE_SIBLING_POWER_DOWN | \
   T5AI_SLEEP_STATE_SIBLING_RESET_ACK | T5AI_SLEEP_STATE_SIBLING_POWER_ACK)

#define T5AI_SLEEP_LPO_HALT_MASK   (1u << 24)
#define T5AI_SLEEP_TRACE_ALIGNMENT 32u
#define T5AI_SLEEP_TRACE_MAGIC     0x53545235u
// Last completed boundary; a marker is not evidence that the next stage finished.
typedef enum {
  T5AISleepStage_None = 0,
  T5AISleepStage_AP_Published = 1,
  T5AISleepStage_AP_Woke = 2,
  T5AISleepStage_AP_ClocksRestored = 3,
  T5AISleepStage_CP_Resources = 16,
  T5AISleepStage_CP_HAL = 17,
  T5AISleepStage_CP_ClocksLow = 18,
  T5AISleepStage_CP_WFI = 19,
  T5AISleepStage_CP_Woke = 20,
  T5AISleepStage_CP_Restored = 21,
  T5AISleepStage_CP_CoreLow = 22,
  T5AISleepStage_CP_PowerConfigured = 23,
  T5AISleepStage_CP_DomainsPrepared = 24,
  T5AISleepStage_CP_VoltagePrepared = 25,
  T5AISleepStage_CP_AnalogPrepared = 26,
  T5AISleepStage_CP_VoltageRestored = 27,
  T5AISleepStage_CP_HFRestored = 28,
  T5AISleepStage_CP_DomainsRestored = 29,
  T5AISleepStage_CP_BspRestored = 30,
  T5AISleepStage_CP_CoreRestored = 31,
  T5AISleepStage_CP_FlashRestored = 32,
  T5AISleepStage_CP_IrqRestored = 33,
  T5AISleepStage_CP_TransitionReady = 34,
  T5AISleepStage_CP_TransitionReturned = 35,
  T5AISleepStage_CP_MaskRestored = 36,
  T5AISleepStage_CP_TickRestored = 37,
  T5AISleepStage_CP_HALReturned = 38,
  T5AISleepStage_CP_ResourceRestore = 39,
  T5AISleepStage_CP_BakpRestored = 40,
  T5AISleepStage_CP_CallbacksRestored = 41,
  T5AISleepStage_CP_AppVetoRestored = 42,
  T5AISleepStage_CP_ReadyRevoked = 43,
  T5AISleepStage_CP_ResumeQueued = 44,
  T5AISleepStage_CP_FlashControllerRestored = 45,
  T5AISleepStage_CP_FlashResumeFailed = 46,
  T5AISleepStage_CP_AdmissionSnapshot = 47,
  T5AISleepStage_CP_AnalogWakePrepared = 48,
  T5AISleepStage_CP_AnalogWakeRestored = 49,
  T5AISleepStage_CP_MaskBarrierEnter = 50,
  T5AISleepStage_CP_MaskBarrierReturned = 51,
  T5AISleepStage_CP_ResumeSent = 52,
  T5AISleepStage_CP_InterruptsReleased = 53,
  T5AISleepStage_CP_CallbackEnterBase = 64,
  T5AISleepStage_CP_CallbackExitBase = 128,
} T5AISleepStage;

#define T5AI_SLEEP_HAL_REJECT_NVIC   (1u << 0)
#define T5AI_SLEEP_HAL_REJECT_LOW    (1u << 1)
#define T5AI_SLEEP_HAL_REJECT_HIGH   (1u << 2)
#define T5AI_SLEEP_HAL_REJECT_TICK   (1u << 3)
#define T5AI_SLEEP_HAL_REJECT_PERMIT (1u << 4)

#define T5AI_SLEEP_HAL_REJECT_PROBE (1u << 5)

// Bounded diagnostic UART snapshots, emitted only during keep-HF controls.
#define T5AI_SLEEP_SNAPSHOT_MARKER 0x1fu
typedef enum {
  T5AISleepSnapshot_APControl = 0,
  T5AISleepSnapshot_APIRQEnableHigh = 1,
  T5AISleepSnapshot_APIRQStatusHigh = 2,
  T5AISleepSnapshot_Coordination = 3,
  T5AISleepSnapshot_CoreClock = 4,
  T5AISleepSnapshot_MemoryClock = 5,
  T5AISleepSnapshot_CPUStatus = 6,
  T5AISleepSnapshot_CPControl = 7,
  T5AISleepSnapshot_CPIRQEnableHigh = 8,
  T5AISleepSnapshot_CPIRQStatusHigh = 9,
  T5AISleepSnapshot_CPNVICPendingHigh = 10,
  T5AISleepSnapshot_CPInterruptControl = 11,
  T5AISleepSnapshot_MailboxState = 12,
  T5AISleepSnapshot_MailboxTXCount = 13,
  T5AISleepSnapshot_MailboxACKCount = 14,
  T5AISleepSnapshot_APMailboxControlAfter = 15,
  T5AISleepSnapshot_APMailboxFIFOAfter = 16,
  T5AISleepSnapshot_APMailboxControlBefore = 17,
  T5AISleepSnapshot_APMailboxFIFOBefore = 18,
  T5AISleepSnapshot_SleepConfig = 19,
  T5AISleepSnapshot_WakeConfig = 20,
  T5AISleepSnapshot_APMailboxConfigBefore = 21,
  T5AISleepSnapshot_APMailboxConfigAfter = 22,
  T5AISleepSnapshot_SleepConditionBefore = 23,
  T5AISleepSnapshot_SleepConfigRestored = 24,
  T5AISleepSnapshot_MailboxInterruptStatus = 25,
  T5AISleepSnapshot_MailboxGlobalControl = 26,
  T5AISleepSnapshot_UARTConfig = 27,
  T5AISleepSnapshot_UARTStatus = 28,
  T5AISleepSnapshot_PowerParameter = 29,
  T5AISleepSnapshot_HFBeforeMask = 30,
  T5AISleepSnapshot_CPUSleepControl = 31,
  T5AISleepSnapshot_CPCacheRetention = 32,
  T5AISleepSnapshot_MemoryRetention = 33,
  T5AISleepSnapshot_RTCControl = 34,
} T5AISleepSnapshot;

// Test-only CP reset-reason namespace; vendor causes occupy 0x00..0x18 and 0xff.
#define T5AI_SLEEP_CP_TRACE_REASON_BASE   0x80u
#define T5AI_SLEEP_CP_TRACE_REASON_MASK   0x80u
#define T5AI_SLEEP_CP_TRACE_MASK_RETURNED 0x40u
#define T5AI_SLEEP_CP_TRACE_STAGE_MASK    0x3fu
#define T5AI_SLEEP_FINAL_REJECT_TRACE     (1u << 8)

#define T5AI_SLEEP_ANALOG_WAKE_PERIOD_MS         32u
#define T5AI_SLEEP_ANALOG_WAKE_LEAD_MS           1u
#define T5AI_SLEEP_ANALOG_WAKE_REQUEST_BUDGET_MS 1u
#define T5AI_SLEEP_ANALOG_WAKE_MIN_MS \
  (T5AI_SLEEP_ANALOG_WAKE_PERIOD_MS + T5AI_SLEEP_ANALOG_WAKE_LEAD_MS)
#define T5AI_SLEEP_ANALOG_WAKE_MARGIN_MS     8u
#define T5AI_SLEEP_FINAL_REJECT_ANALOG_WAKE  (1u << 9)
#define T5AI_SLEEP_ANALOG_STATE_ENABLED      (1u << 0)
#define T5AI_SLEEP_ANALOG_STATE_PERIOD_SHIFT 4u
#define T5AI_SLEEP_ANALOG_STATE_CLOCK_SHIFT  8u
#define T5AI_SLEEP_ANALOG_STATE_LATCH_SHIFT  9u

#define T5AI_SLEEP_ANALOG_PREPARE_REJECT_OWNER    (1u << 0)
#define T5AI_SLEEP_ANALOG_PREPARE_REJECT_DEADLINE (1u << 1)

// Diagnostic-only receive snapshots; sequence/inverse commit one cache line.
#define T5AI_RECEIVE_TRACE_MAGIC         0x52585431u
#define T5AI_RECEIVE_TRACE_VERSION       1u
#define T5AI_RECEIVE_TRACE_SEQUENCE_STEP 2u
#define T5AI_RECEIVE_TRACE_IN_PROGRESS   1u
typedef enum {
  T5AIReceive_None = 0,
  T5AIReceive_AdapterEnter,
  T5AIReceive_CopyEnter,
  T5AIReceive_CopyReturned,
  T5AIReceive_CallbackEnter,
  T5AIReceive_ACKEnter,
  T5AIReceive_CompletionEnter,
  T5AIReceive_CompletionReturned,
  T5AIReceive_NextSendEnter,
  T5AIReceive_NextSendReturned,
  T5AIReceive_ACKReturned,
  T5AIReceive_CommandEnter,
  T5AIReceive_CommandReturned,
  T5AIReceive_CallbackReturned,
  T5AIReceive_DriverReturned,
  T5AIReceive_Rejected,
} T5AIReceiveStage;

// Passive CP idle-port boundaries; no suspend/wake policy is encoded here.
#define T5AI_SLEEP_PORT_PROGRESS_MASK 0x0fu
typedef enum {
  T5AIPort_None,
  T5AIPort_Enter,
  T5AIPort_GuardRefused,
  T5AIPort_TickStopped,
  T5AIPort_CriticalEntered,
  T5AIPort_AbortRestored,
  T5AIPort_PMEnter,
  T5AIPort_PMReturned,
  T5AIPort_FirstTickEnter,
  T5AIPort_FirstTickReturned,
  T5AIPort_InterruptWindowReturned,
  T5AIPort_SecondTickEnter,
  T5AIPort_SecondTickReturned,
  T5AIPort_CriticalExited,
  T5AIPort_APReleaseEnter,
  T5AIPort_Returned,
} T5AIPortProgress;

typedef struct {
  uint32_t magic, sequence, generation, stage, header, payload, inverse, version;
} T5AIReceiveTrace;

// CP cleanup breadcrumbs; the existing ISR retains FIFO and ACK ownership.
typedef enum {
  T5AIMailboxProgress_None = 0,
  T5AIMailboxProgress_ISREnter,
  T5AIMailboxProgress_FIFOStatus,
  T5AIMailboxProgress_Receive,
  T5AIMailboxProgress_Callback,
  T5AIMailboxProgress_CallbackReturned,
  T5AIMailboxProgress_ISRReturn,
  T5AIMailboxProgress_RxCommand,
  T5AIMailboxProgress_LogicalCallback,
  T5AIMailboxProgress_LogicalCallbackReturned,
  T5AIMailboxProgress_ACKSend,
  T5AIMailboxProgress_ACKSent,
  T5AIMailboxProgress_PMQueue,
  T5AIMailboxProgress_PMQueued,
  T5AIMailboxProgress_NativeRequest,
  T5AIMailboxProgress_ACKFailed,
} T5AIMailboxProgress;

// Cancel progress survives cleanup release; reply submission is not peer receipt.
typedef enum {
  T5AICancelProgress_None = 0,
  T5AICancelProgress_Received,
  T5AICancelProgress_GenerationMatched,
  T5AICancelProgress_Locked,
  T5AICancelProgress_VetoRestored,
  T5AICancelProgress_ReadyRevoked,
  T5AICancelProgress_Disarmed,
  T5AICancelProgress_Unlocked,
  T5AICancelProgress_Unregister,
  T5AICancelProgress_UnregisterReturned,
  T5AICancelProgress_PersistentChecked,
  T5AICancelProgress_CleanupReleased,
  T5AICancelProgress_Reply,
  T5AICancelProgress_ReplySubmitted,
  T5AICancelProgress_ReplyFailed,
  T5AICancelProgress_ResourcesActive,
} T5AICancelProgress;
