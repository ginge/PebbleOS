/* SPDX-License-Identifier: Apache-2.0 */
// Included only in the isolated CP sleep-contract overlay.
#include "aon_pmu_ll.h"
#include "armstar.h"
#include "bk_arch.h"
#include "sys_ll.h"

#include <limits.h>

#include <driver/aon_rtc.h>
#include <driver/int_types.h>
#include <driver/unhygienix_sleep_protocol.h>

const uint32_t unhygienix_native_alarm_contract[] = {
  T5AI_PM_NATIVE_ALARM_ABI,       T5AI_PM_NATIVE_ALARM_COMMAND,   T5AI_PM_NATIVE_ALARM_WAKE,
  T5AI_PM_NATIVE_ALARM_MIN_TICKS, T5AI_PM_NATIVE_ALARM_MAX_TICKS,
};

#define NATIVE_MAILBOX_GLOBAL_CONTROL   (*(volatile uint32_t *)0x41000008u)
#define NATIVE_MAILBOX_INTERRUPT_STATUS (*(volatile uint32_t *)0x4100000cu)
#define NATIVE_MAILBOX_CP_CONTROL       (*(volatile uint32_t *)0x41000040u)
#define NATIVE_MAILBOX_INTERRUPT_ENABLE (1u << 8)
#define NATIVE_MAILBOX_FIFO_NOT_EMPTY   (1u << 19)
#define NATIVE_MAILBOX_CP_ROUTE         (1u << (INT_SRC_MAILBOX - 32u))
#define NATIVE_CLEANUP_CORE_ENABLED     (1u << 0)
#define NATIVE_CLEANUP_NVIC_ENABLED     (1u << 1)
#define NATIVE_CLEANUP_FIFO_QUEUED      (1u << 2)
#define NATIVE_CLEANUP_ROUTE_ENABLED    (1u << 3)
#define NATIVE_CLEANUP_IRQ_PENDING      (1u << 4)
#define NATIVE_CLEANUP_DELIVERY_READY                                                       \
  (NATIVE_CLEANUP_CORE_ENABLED | NATIVE_CLEANUP_NVIC_ENABLED | NATIVE_CLEANUP_FIFO_QUEUED | \
   NATIVE_CLEANUP_ROUTE_ENABLED)
#define NATIVE_MAILBOX_AP_CONTROL     (*(volatile uint32_t *)0x41000080u)
#define NATIVE_MAILBOX_AP_FIFO_CONFIG (*(volatile uint32_t *)0x41000084u)
#define NATIVE_MAILBOX_AP_FIFO_STATUS (*(volatile uint32_t *)0x410000a0u)
static uint32_t s_mailbox_ap_control_before, s_mailbox_ap_fifo_before;
static uint32_t s_mailbox_ap_fifo_config_before;

#define NATIVE_ALARM_NAME                "pbl_ap"
#define NATIVE_ALARM_ONESHOT             1u
#define NATIVE_AON_COUNT_LOW             (*(volatile uint32_t *)0x4400020cu)
#define NATIVE_GPIO_STATUS_LOW           (*(volatile uint32_t *)0x44000500u)
#define NATIVE_GPIO_STATUS_HIGH          (*(volatile uint32_t *)0x44000504u)
#define NATIVE_AON_TICKS_PER_MS          32u
#define NATIVE_AP_PUBLICATION_WAIT_MS    3u
#define NATIVE_AP_PUBLICATION_WAIT_TICKS (NATIVE_AP_PUBLICATION_WAIT_MS * NATIVE_AON_TICKS_PER_MS)

static volatile uint32_t s_native_generation;
static volatile uint32_t s_native_fired;
static volatile bool s_native_armed;
static volatile bool s_native_cancelled;
static volatile bool s_native_faulted;
volatile bool unhygienix_native_sleep_prepared;
volatile uint32_t unhygienix_native_lowvol_count;
static volatile bool s_native_permit;
static bool s_native_wake_only, s_native_resources_only, s_native_clocks_only;
static bool s_native_domain_bypass, s_native_shallow_wfi, s_native_mask_barrier;
static uint32_t s_mask_barrier_count;
static bool s_mask_barrier_returned;
static bool s_native_no_hw_mask;
static bool s_native_system_mask_used;
static volatile uint32_t s_native_normal_sleep_skips;

// The pinned HAL warns that shallow WFI cannot wake after using the system mask.
bool unhygienix_native_normal_sleep_allowed(void) {
  if (!s_native_system_mask_used)
    return true;
  if (s_native_normal_sleep_skips != UINT32_MAX)
    ++s_native_normal_sleep_skips;
  return false;
}
static bool s_native_persistent_trace, s_native_analog_wake;
static bool s_analog_owned;
static uint32_t s_analog_prepare_lead, s_analog_prepare_reject;
static uint32_t s_analog_period_before, s_analog_clock_before, s_analog_start;
#define NATIVE_ANALOG_WAKE_PERIOD_SELECT 0u
#define NATIVE_ANALOG_WAKE_MIN_TICKS     (T5AI_SLEEP_ANALOG_WAKE_MIN_MS * NATIVE_AON_TICKS_PER_MS)
#define NATIVE_ANALOG_WAKE_REQUIRED_LEAD_TICKS \
  ((T5AI_SLEEP_ANALOG_WAKE_PERIOD_MS + T5AI_SLEEP_ANALOG_WAKE_LEAD_MS) * NATIVE_AON_TICKS_PER_MS)
#define NATIVE_ANALOG_WAKE_ENTRY_BUDGET_TICKS \
  ((T5AI_SLEEP_ANALOG_WAKE_PERIOD_MS - T5AI_SLEEP_ANALOG_WAKE_MARGIN_MS) * NATIVE_AON_TICKS_PER_MS)
static bool s_persistent_saved, s_persistent_recording;
static uint32_t s_persistent_reason_before, s_persistent_reason_last;
static bool s_native_vendor_flash_idle;
static bool s_native_vendor_cpu_idle;
static bool s_native_vendor_subcores_on;
static bool s_native_vendor_masked;
static bool s_native_vendor_lpo_halt;
static bool s_native_retain_modules;
static bool s_native_mask_only;
static bool s_native_keep_hf, s_native_core_only, s_native_flash_restore, s_native_bsp_restore;
static uint32_t s_native_deadline;
static uint32_t s_clock_count, s_final_lead, s_sleep_config, s_wake_config;
static uint32_t s_clock_mode_low, s_hf_low, s_final_pending_low, s_final_pending_high;
static uint32_t s_final_nvic_low, s_final_nvic_high;
static uint32_t s_final_ap_pending_low, s_final_ap_pending_high;
static uint32_t s_final_gpio_status_low, s_final_gpio_status_high;
static uint32_t s_final_reject, s_final_skips, s_mask_before, s_mask_after, s_mask_at_final;
static bool s_mask_saved;
static uint32_t s_power_parameter, s_wake_status_before, s_wake_status_after;
static uint32_t s_pmu_wake_count, s_pmu_wake_sources, s_pmu_wake_ticks, s_pmu_ambiguous_count;
static uint32_t s_wake_clear_before, s_wake_clear_after, s_pmu_clear_held_count, s_mask_after_wfi;
#define NATIVE_PMU_WAKE_CLEAR_ASSERTED (1u << 17)
#define NATIVE_PMU_WAKE_SOURCE_SHIFT   20u
#define NATIVE_PMU_WAKE_SOURCE_BITS    6u
#define NATIVE_PMU_WAKE_SOURCE_MASK \
  (((1u << NATIVE_PMU_WAKE_SOURCE_BITS) - 1u) << NATIVE_PMU_WAKE_SOURCE_SHIFT)
#define NATIVE_SLEEP_NEEDS_FLASH_IDLE (1u << 16)
#define NATIVE_SLEEP_NEEDS_AP_WFI     (1u << 17)
#define NATIVE_SLEEP_NEEDS_CP_WFI     (1u << 18)
#define NATIVE_SLEEP_GLOBAL_ENABLE    (1u << 19)
#define NATIVE_SLEEP_BUS_IDLE_BYPASS  (1u << 20)
#define NATIVE_SLEEP_NEEDS_CPU2_WFI   (1u << 21)
#define NATIVE_SLEEP_CONDITION_MASK                                                        \
  (NATIVE_SLEEP_NEEDS_FLASH_IDLE | NATIVE_SLEEP_NEEDS_AP_WFI | NATIVE_SLEEP_NEEDS_CP_WFI | \
   NATIVE_SLEEP_GLOBAL_ENABLE | NATIVE_SLEEP_BUS_IDLE_BYPASS | NATIVE_SLEEP_NEEDS_CPU2_WFI)
static uint32_t s_sleep_condition_before;
static uint32_t s_pmu_power_parameter_before;
static uint32_t s_pmu_power_parameter_after;
static uint32_t s_irq_low_before, s_irq_low_after, s_irq_high_before, s_irq_high_after;
static uint32_t s_peripheral_clocks_before, s_peripheral_clocks_after;
static bool s_sleep_condition_saved;
#define NATIVE_SYSTICK_PENDING_MASK           (1u << 26)
#define NATIVE_FLASH_OPERATION_CONTROL_OFFSET 0x10u
#define NATIVE_FLASH_OPERATION_CONTROL \
  (*(volatile uint32_t *)(SOC_FLASH_REG_BASE + NATIVE_FLASH_OPERATION_CONTROL_OFFSET))
#define NATIVE_FLASH_BUSY (1u << 31)
static uint32_t s_resources_count;
static bool s_native_ready_flag, s_native_app_released;

#define NATIVE_CPU_RESET_ASSERTED 0u
#define NATIVE_CPU_POWERED_DOWN   1u
#define NATIVE_CP_IRQ_MASKED      1u

#define NATIVE_APP_AWAKE         0u
#define NATIVE_APP_SLEEP_ALLOWED 1u

extern bool unhygienix_native_flash_ready(void);
extern bool unhygienix_native_flash_resume(void);
#define NATIVE_CP_CONTROL_ADDRESS 0x44010010u
#define NATIVE_CP_INTERRUPT_MASK  (1u << 2)
extern void unhygienix_cp_masked_wfi(volatile uint32_t *control, uint32_t value);
extern void unhygienix_cp_mask_barrier(volatile uint32_t *control, uint32_t masked,
                                       uint32_t before);
extern uint64_t unhygienix_native_sleep_missing_votes(void);
extern bool unhygienix_native_memory_retained(void);
extern bool unhygienix_native_peripherals_retained(void);
extern bool unhygienix_native_mailbox_sleep_ready(void);
extern bk_err_t unhygienix_native_sleep_notify_ap(uint32_t generation, uint32_t tick);
extern uint32_t unhygienix_native_mailbox_snapshot(T5AISleepSnapshot key);
static uint32_t s_native_ready_samples, s_native_missing_low, s_native_missing_high;

#define NATIVE_RESET_REASON_WORD   (*(volatile uint32_t *)0x44000000u)
#define NATIVE_RESET_REASON_COMMIT (*(volatile uint32_t *)0x44000094u)
#define NATIVE_CP_REASON_SHIFT     4u
#define NATIVE_CP_REASON_MASK      (0xffu << NATIVE_CP_REASON_SHIFT)
#define NATIVE_REASON_COMMIT_KEY_1 0x424b55aau
#define NATIVE_REASON_COMMIT_KEY_2 0xbdb4aa55u
extern void unhygienix_cp_masked_wfi_trace(volatile uint32_t *control, uint32_t value,
                                           uint32_t woke_cp_reason);

__attribute__((section(".itcm_sec_code"))) static void prv_persistent_commit(uint32_t word) {
  NATIVE_RESET_REASON_WORD = word;
  NATIVE_RESET_REASON_COMMIT = NATIVE_REASON_COMMIT_KEY_1;
  NATIVE_RESET_REASON_COMMIT = NATIVE_REASON_COMMIT_KEY_2;
  __DSB();
}

__attribute__((section(".itcm_sec_code"))) static uint32_t prv_persistent_reason(uint32_t word) {
  return word & NATIVE_CP_REASON_MASK;
}

__attribute__((section(".itcm_sec_code"))) static void prv_persistent_stage(uint32_t stage) {
  if (!s_native_persistent_trace || stage > T5AI_SLEEP_CP_TRACE_STAGE_MASK)
    return;
  const uint32_t word = NATIVE_RESET_REASON_WORD;
  if (stage == T5AISleepStage_CP_Resources) {
    if (s_persistent_saved || !aon_pmu_ll_get_r3_cp1_enter_wfi_state()) {
      s_native_faulted = true;
      return;
    }
    s_persistent_reason_before = prv_persistent_reason(word);
    s_persistent_reason_last = s_persistent_reason_before;
    s_persistent_saved = s_persistent_recording = true;
    s_mask_barrier_returned = false;
  }
  if (!s_persistent_recording)
    return;
  if (prv_persistent_reason(word) != s_persistent_reason_last) {
    s_native_faulted = true;
    return;
  }
  const uint32_t returned = s_mask_barrier_returned ? T5AI_SLEEP_CP_TRACE_MASK_RETURNED : 0u;
  s_persistent_reason_last = (T5AI_SLEEP_CP_TRACE_REASON_BASE | returned | stage)
                             << NATIVE_CP_REASON_SHIFT;
  prv_persistent_commit((word & ~NATIVE_CP_REASON_MASK) | s_persistent_reason_last);
  if (stage == T5AISleepStage_CP_AppVetoRestored)
    s_persistent_recording = false; // Stop before CP revokes ready and AP resumes.
}

static bool prv_persistent_restore(void) {
  if (!s_persistent_saved)
    return true;
  const uint32_t word = NATIVE_RESET_REASON_WORD;
  if (prv_persistent_reason(word) != s_persistent_reason_last)
    return false;
  prv_persistent_commit((word & ~NATIVE_CP_REASON_MASK) | s_persistent_reason_before);
  s_persistent_saved = s_persistent_recording = false;
  return true;
}

typedef struct {
  uint32_t magic, generation, stage, inverse;
} NativeSleepTrace;
static volatile NativeSleepTrace s_native_trace
    __attribute__((section(".unhygienix_sleep_trace"), aligned(T5AI_SLEEP_TRACE_ALIGNMENT)));
static uint32_t s_previous_stage, s_previous_generation;
static bool s_trace_captured;
static bool s_boot_pmu_captured;
static uint32_t s_boot_pmu_wake, s_boot_pmu_reset, s_boot_pmu_power, s_boot_pmu_wake_config;

// Called after BSS setup, before reset_reason_init consumes the boot snapshot.
void unhygienix_native_sleep_boot_capture(void) {
  if (s_boot_pmu_captured)
    return;
  s_boot_pmu_wake = aon_pmu_ll_get_r71();
  s_boot_pmu_reset = aon_pmu_ll_get_r7a();
  s_boot_pmu_power = aon_pmu_ll_get_r40();
  s_boot_pmu_wake_config = aon_pmu_ll_get_r41();
  s_boot_pmu_captured = true;
}

static bool s_resources_active;
static bool s_native_resume_pending;
_Static_assert(sizeof(T5AIReceiveTrace) == T5AI_SLEEP_TRACE_ALIGNMENT,
               "Receive snapshot must fill one shared cache line");
static volatile T5AIReceiveTrace s_native_receive_trace
    __attribute__((aligned(T5AI_SLEEP_TRACE_ALIGNMENT)));

// Mailbox ISR is the sole writer; cleanup retirement does not stop observation.
void unhygienix_native_mailbox_receive_observe(uint32_t stage, uint32_t header, uint32_t payload) {
  if (!s_native_generation || stage == T5AIReceive_None || stage > T5AIReceive_Rejected)
    return;
  const uint32_t sequence = (s_native_receive_trace.sequence & ~T5AI_RECEIVE_TRACE_IN_PROGRESS) +
                            T5AI_RECEIVE_TRACE_SEQUENCE_STEP;
  s_native_receive_trace.sequence = sequence | T5AI_RECEIVE_TRACE_IN_PROGRESS;
  __DMB();
  s_native_receive_trace.magic = T5AI_RECEIVE_TRACE_MAGIC;
  s_native_receive_trace.version = T5AI_RECEIVE_TRACE_VERSION;
  s_native_receive_trace.generation = s_native_generation;
  s_native_receive_trace.stage = stage;
  if (stage == T5AIReceive_AdapterEnter) {
    s_native_receive_trace.header = 0u;
    s_native_receive_trace.payload = payload;
  } else if (header) {
    s_native_receive_trace.header = header;
  }
  s_native_receive_trace.inverse = ~sequence;
  __DMB();
  s_native_receive_trace.sequence = sequence;
}

_Static_assert(T5AIPort_Returned <= T5AI_SLEEP_PORT_PROGRESS_MASK,
               "Port progress encoding overflow");
static volatile uint32_t s_native_port_progress
    __attribute__((aligned(T5AI_SLEEP_TRACE_ALIGNMENT)));

// The idle task is the sole writer; AP reads one aligned word without ownership.
void unhygienix_native_sleep_port_progress(uint32_t progress) {
  if (s_native_generation && progress <= T5AIPort_Returned) {
    s_native_port_progress = progress;
    __DMB();
  }
}

static volatile bool s_native_cleanup_pending;
static volatile bool s_cleanup_reply_pending;
static uint32_t s_cleanup_reply_generation;
static uint32_t s_cleanup_idle_refusals;
static volatile uint32_t s_cleanup_irq_state;
static uint32_t s_cleanup_irq_kicks;
static volatile uint32_t s_cleanup_irq_delivery;
static volatile uint32_t s_cleanup_mailbox_progress;
static volatile uint32_t s_cleanup_cancel_progress;

// PWC serialization and the pre-suspend drain retire older native replies.
void unhygienix_native_sleep_reply_ack(uint32_t command, uint32_t generation, uint32_t abi,
                                       uint32_t communication_state) {
  if (!s_cleanup_reply_pending || command != T5AI_PM_NATIVE_ALARM_COMMAND ||
      generation != s_cleanup_reply_generation || abi != T5AI_PM_NATIVE_ALARM_ABI)
    return;
  if (communication_state != 0u) {
    s_native_faulted = true;
    return;
  }
  s_cleanup_reply_pending = false;
  s_native_cleanup_pending = false;
  s_cleanup_cancel_progress = T5AICancelProgress_CleanupReleased;
}

// Keep the original ISR's boundary visible after its cleanup lease closes.
void unhygienix_native_sleep_mailbox_progress(uint32_t progress) {
  if (s_native_generation != 0u && progress <= T5AIMailboxProgress_ACKFailed)
    s_cleanup_mailbox_progress = progress;
}

static volatile uint32_t s_cleanup_mailbox_isrs, s_cleanup_mailbox_empty_status;
#define NATIVE_DELIVERY_FAULT_MASK   (1u << 0)
#define NATIVE_DELIVERY_NVIC_PENDING (1u << 1)
#define NATIVE_DELIVERY_ISR_SEEN     (1u << 2)
#define NATIVE_DELIVERY_EMPTY_STATUS (1u << 3)

static void prv_cleanup_delivery_snapshot(void) {
  s_cleanup_irq_delivery =
      (__get_FAULTMASK() ? NATIVE_DELIVERY_FAULT_MASK : 0u) |
      (NVIC_GetPendingIRQ((uint32_t)INT_SRC_MAILBOX) ? NATIVE_DELIVERY_NVIC_PENDING : 0u) |
      (s_cleanup_mailbox_isrs ? NATIVE_DELIVERY_ISR_SEEN : 0u) |
      (s_cleanup_mailbox_empty_status ? NATIVE_DELIVERY_EMPTY_STATUS : 0u);
}

void unhygienix_native_sleep_mailbox_irq_observed(uint32_t status) {
  if (!s_native_cleanup_pending)
    return;
  if (s_cleanup_mailbox_isrs != UINT32_MAX)
    ++s_cleanup_mailbox_isrs;
  if (!status && s_cleanup_mailbox_empty_status != UINT32_MAX)
    ++s_cleanup_mailbox_empty_status;
  prv_cleanup_delivery_snapshot();
}
static bool s_lpo_saved;
static uint32_t s_lpo_halt;
static uint32_t s_resource_tick, s_wfi_tick, s_lowvol_ticks, s_prepare_ticks;
static uint32_t s_hal_reject, s_hal_pending_low, s_hal_pending_high;
static uint32_t s_hal_status_low, s_hal_status_high;

extern bk_err_t gpio_enter_low_power(void *param);
extern bk_err_t gpio_exit_low_power(void *param);
extern bk_err_t gpio_enable_interrupt_mult_for_wake(void);

bk_err_t unhygienix_native_gpio_enter(void *param) {
  return unhygienix_native_sleep_prepared ? BK_OK : gpio_enter_low_power(param);
}
bk_err_t unhygienix_native_gpio_exit(void *param) {
  return unhygienix_native_sleep_prepared ? BK_OK : gpio_exit_low_power(param);
}
bk_err_t unhygienix_native_gpio_wake(void) {
  return unhygienix_native_sleep_prepared ? BK_OK : gpio_enable_interrupt_mult_for_wake();
}

bool unhygienix_native_sleep_subcores_ready(void);
bool unhygienix_native_sleep_hal_ready(uint64_t pending, uint32_t low, uint32_t high,
                                       uint32_t tick) {
  uint32_t rejected =
      (pending ? T5AI_SLEEP_HAL_REJECT_NVIC : 0u) | (low ? T5AI_SLEEP_HAL_REJECT_LOW : 0u) |
      (high ? T5AI_SLEEP_HAL_REJECT_HIGH : 0u) | (tick ? T5AI_SLEEP_HAL_REJECT_TICK : 0u);
  if (!rejected && unhygienix_native_sleep_prepared && !unhygienix_native_sleep_subcores_ready())
    rejected |= T5AI_SLEEP_HAL_REJECT_PERMIT;
  if (!rejected && unhygienix_native_sleep_prepared && s_native_resources_only)
    rejected |= T5AI_SLEEP_HAL_REJECT_PROBE;
  if (unhygienix_native_sleep_prepared) {
    s_prepare_ticks = NATIVE_AON_COUNT_LOW - s_resource_tick;
    s_hal_reject = rejected;
    s_hal_pending_low = (uint32_t)pending;
    s_hal_pending_high = (uint32_t)(pending >> 32);
    s_hal_status_low = low;
    s_hal_status_high = high;
  }
  return rejected == 0u;
}
__attribute__((section(".itcm_sec_code"))) uint32_t
unhygienix_native_sleep_halt_config(uint32_t value) {
  if (!unhygienix_native_sleep_prepared)
    return value;
  if (!s_lpo_saved) {
    s_lpo_halt = aon_pmu_ll_get_r41() & T5AI_SLEEP_LPO_HALT_MASK;
    s_lpo_saved = true;
  }
  return s_native_vendor_lpo_halt ? value : value & ~T5AI_SLEEP_LPO_HALT_MASK;
}

#define NATIVE_TRACE_UART_BASE           0x45830000u
#define NATIVE_TRACE_UART_GLOBAL_OFFSET  0x08u
#define NATIVE_TRACE_UART_CONFIG_OFFSET  0x10u
#define NATIVE_TRACE_UART_STATUS_OFFSET  0x18u
#define NATIVE_TRACE_UART_FIFO_OFFSET    0x1cu
#define NATIVE_TRACE_UART_RESET_RELEASED (1u << 0)
#define NATIVE_TRACE_UART_TX_ENABLED     (1u << 0)
#define NATIVE_TRACE_UART_TX_EMPTY       (1u << 17)
#define NATIVE_TRACE_UART_WRITE_READY    (1u << 20)
#define NATIVE_TRACE_MARKER              0x1eu
static bool s_trace_uart_owned;
#define NATIVE_TRACE_UART_REGISTER(offset) \
  (*(volatile uint32_t *)(NATIVE_TRACE_UART_BASE + (offset)))

// Called after the shell has suspended its queue/IRQ and drained the last byte.
__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_uart_trace_claim(
    uint32_t uart_id) {
  const uint32_t console_uart = 1u;
  if (!__get_PRIMASK() || uart_id != console_uart ||
      !sys_ll_get_cpu_device_clk_enable_uart1_cken() || !unhygienix_native_sleep_prepared ||
      !s_native_keep_hf || s_native_core_only || !aon_pmu_ll_get_r3_cp1_enter_wfi_state() ||
      !(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_GLOBAL_OFFSET) &
        NATIVE_TRACE_UART_RESET_RELEASED) ||
      !(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_CONFIG_OFFSET) &
        NATIVE_TRACE_UART_TX_ENABLED) ||
      !(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_STATUS_OFFSET) & NATIVE_TRACE_UART_TX_EMPTY))
    return false;
  s_trace_uart_owned = true;
  return true;
}

#define NATIVE_TRACE_MAX_FRAME_BYTES 8u
#define NATIVE_TRACE_HEADER_BYTES    2u
#define NATIVE_TRACE_TRAILER_BYTES   2u

// Only the CP critical section owns the shell writer during this bounded lease.
__attribute__((section(".itcm_sec_code"))) static void prv_native_trace_bytes(const uint8_t *bytes,
                                                                              uint32_t length) {
  if (length > NATIVE_TRACE_MAX_FRAME_BYTES || !__get_PRIMASK() || !s_trace_uart_owned ||
      !sys_ll_get_cpu_device_clk_enable_uart1_cken() || !s_native_keep_hf || s_native_core_only ||
      (s_resources_active && !aon_pmu_ll_get_r3_cp1_enter_wfi_state()) ||
      !(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_GLOBAL_OFFSET) &
        NATIVE_TRACE_UART_RESET_RELEASED) ||
      !(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_CONFIG_OFFSET) & NATIVE_TRACE_UART_TX_ENABLED))
    return;
  for (unsigned i = 0; i < length; ++i) {
    if (!(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_STATUS_OFFSET) &
          NATIVE_TRACE_UART_WRITE_READY))
      return;
    NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_FIFO_OFFSET) = bytes[i];
  }
}

__attribute__((section(".itcm_sec_code"))) static void prv_native_trace_uart(uint32_t stage) {
  // Reserve FIFO room for the bounded final-handoff register snapshot.
  switch (stage) {
    case T5AISleepStage_CP_Woke:
    case T5AISleepStage_CP_FlashControllerRestored:
    case T5AISleepStage_CP_FlashResumeFailed:
    case T5AISleepStage_CP_HAL:
    case T5AISleepStage_CP_AdmissionSnapshot:
    case T5AISleepStage_CP_Restored:
    case T5AISleepStage_CP_AppVetoRestored:
    case T5AISleepStage_CP_ReadyRevoked:
    case T5AISleepStage_CP_ResumeQueued:
      break;
    default:
      return;
  }
  const uint8_t bytes[] = {NATIVE_TRACE_MARKER, (uint8_t)stage, (uint8_t)~stage, '\n'};
  prv_native_trace_bytes(bytes, sizeof(bytes));
}

// Keep the fixed-size diagnostic frame independent of flash failure handlers.
__attribute__((section(".itcm_sec_code"), optimize("-fno-stack-protector"))) static void
prv_native_trace_word(T5AISleepSnapshot key, uint32_t value) {
  uint8_t bytes[NATIVE_TRACE_HEADER_BYTES + sizeof(value) + NATIVE_TRACE_TRAILER_BYTES];
  bytes[0] = T5AI_SLEEP_SNAPSHOT_MARKER;
  bytes[1] = (uint8_t)key;
  uint8_t checksum = bytes[0] ^ bytes[1];
  for (unsigned i = 0; i < sizeof(value); ++i) {
    bytes[NATIVE_TRACE_HEADER_BYTES + i] = (uint8_t)(value >> (i * CHAR_BIT));
    checksum ^= bytes[NATIVE_TRACE_HEADER_BYTES + i];
  }
  bytes[sizeof(bytes) - NATIVE_TRACE_TRAILER_BYTES] = checksum;
  bytes[sizeof(bytes) - 1u] = '\n';
  prv_native_trace_bytes(bytes, sizeof(bytes));
}

__attribute__((section(".itcm_sec_code"))) static void prv_native_trace_snapshot(void) {
  if (!s_trace_uart_owned || !__get_PRIMASK())
    return;
  prv_native_trace_word(T5AISleepSnapshot_UARTConfig,
                        NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_CONFIG_OFFSET));
  prv_native_trace_word(T5AISleepSnapshot_SleepConfig, s_sleep_config);
  prv_native_trace_word(T5AISleepSnapshot_CPControl, sys_ll_get_cpu0_int_halt_clk_op_value());
  prv_native_trace_word(T5AISleepSnapshot_CPIRQEnableHigh, sys_ll_get_cpu0_int_32_63_en_value());
  prv_native_trace_word(T5AISleepSnapshot_MailboxInterruptStatus, NATIVE_MAILBOX_INTERRUPT_STATUS);
  prv_native_trace_word(T5AISleepSnapshot_MailboxGlobalControl, NATIVE_MAILBOX_GLOBAL_CONTROL);
  prv_native_trace_word(T5AISleepSnapshot_APIRQEnableHigh, sys_ll_get_cpu1_int_32_63_en_value());
  prv_native_trace_word(T5AISleepSnapshot_APIRQStatusHigh,
                        sys_ll_get_cpu1_int_32_63_status_value());
  prv_native_trace_word(T5AISleepSnapshot_Coordination, aon_pmu_ll_get_r3());
  prv_native_trace_word(T5AISleepSnapshot_UARTStatus,
                        NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_STATUS_OFFSET));
  prv_native_trace_word(T5AISleepSnapshot_APMailboxControlAfter, NATIVE_MAILBOX_AP_CONTROL);
  prv_native_trace_word(T5AISleepSnapshot_APMailboxFIFOAfter, NATIVE_MAILBOX_AP_FIFO_STATUS);
}

__attribute__((section(".itcm_sec_code"))) void unhygienix_native_sleep_stage(uint32_t stage) {
  if (!unhygienix_native_sleep_prepared)
    return;
  prv_persistent_stage(stage);
  if (stage == T5AISleepStage_CP_Resources) {
    s_sleep_condition_before =
        sys_ll_get_cpu_power_sleep_wakeup_value() & NATIVE_SLEEP_CONDITION_MASK;
    s_pmu_power_parameter_before = aon_pmu_ll_get_r40();
    s_irq_low_before = sys_ll_get_cpu0_int_0_31_en_value();
    s_irq_high_before = sys_ll_get_cpu0_int_32_63_en_value();
    s_peripheral_clocks_before = sys_ll_get_cpu_device_clk_enable_value();
    s_sleep_condition_saved = true;
    s_mailbox_ap_fifo_config_before = NATIVE_MAILBOX_AP_FIFO_CONFIG;
    s_mailbox_ap_control_before = NATIVE_MAILBOX_AP_CONTROL;
    s_mailbox_ap_fifo_before = NATIVE_MAILBOX_AP_FIFO_STATUS;
    s_trace_uart_owned = false;
    s_resources_active = true;
    ++s_resources_count;
    s_resource_tick = NATIVE_AON_COUNT_LOW;
  } else if (stage == T5AISleepStage_CP_Restored) {
    s_resources_active = false;
  }
  if (stage == T5AISleepStage_CP_WFI)
    s_wfi_tick = NATIVE_AON_COUNT_LOW;
  else if (stage == T5AISleepStage_CP_Woke) {
    const uint32_t elapsed = NATIVE_AON_COUNT_LOW - s_wfi_tick;
    s_lowvol_ticks += elapsed;
    const uint32_t source = s_wake_status_after & NATIVE_PMU_WAKE_SOURCE_MASK;
    if (s_wake_status_before & NATIVE_PMU_WAKE_SOURCE_MASK) {
      ++s_pmu_ambiguous_count;
    } else if (source) {
      ++s_pmu_wake_count;
      s_pmu_wake_sources |= source >> NATIVE_PMU_WAKE_SOURCE_SHIFT;
      s_pmu_wake_ticks += elapsed;
    }
  }
  prv_native_trace_uart(stage);
  if (stage == T5AISleepStage_CP_ResumeQueued)
    s_trace_uart_owned = false;
  s_native_trace.magic = 0u;
  s_native_trace.generation = s_native_generation;
  s_native_trace.stage = stage;
  s_native_trace.inverse = ~stage;
  s_native_trace.magic = T5AI_SLEEP_TRACE_MAGIC;
  __DSB();
  SCB->DCCMVAC = (uint32_t)(uintptr_t)&s_native_trace;
  __DSB();
}

// Flash-safe resource preparation only; AP is published and CP IRQs are masked.
void unhygienix_native_analog_wake_prepare(void) {
  if (!s_native_analog_wake || !unhygienix_native_sleep_prepared)
    return;
  s_analog_prepare_reject = 0u;
  if (!__get_PRIMASK() || !aon_pmu_ll_get_r3_cp1_enter_wfi_state() || s_analog_owned ||
      sys_ll_get_ana_reg10_spi_timerwken() || sys_ll_get_ana_reg9_spi_latch1v()) {
    s_analog_prepare_reject = T5AI_SLEEP_ANALOG_PREPARE_REJECT_OWNER;
    s_native_faulted = true;
    return;
  }
  // Publication can be delayed after arming; never move wake beyond its deadline.
  s_analog_prepare_lead = s_native_deadline - NATIVE_AON_COUNT_LOW;
  if ((int32_t)s_analog_prepare_lead < (int32_t)NATIVE_ANALOG_WAKE_REQUIRED_LEAD_TICKS) {
    s_analog_prepare_reject = T5AI_SLEEP_ANALOG_PREPARE_REJECT_DEADLINE;
    s_native_permit = false;
    return;
  }
  s_analog_period_before = sys_ll_get_ana_reg10_timer_sel();
  s_analog_clock_before = sys_ll_get_ana_reg12_clk_sel();
  s_analog_owned = true;
  sys_ll_set_ana_reg9_spi_latch1v(1u);
  sys_ll_set_ana_reg10_spi_timerwken(1u);
  sys_ll_set_ana_reg10_timer_sel(NATIVE_ANALOG_WAKE_PERIOD_SELECT);
  sys_ll_set_ana_reg9_spi_latch1v(0u);
  s_analog_start = NATIVE_AON_COUNT_LOW;
  if (!sys_ll_get_ana_reg10_spi_timerwken() ||
      sys_ll_get_ana_reg10_timer_sel() != NATIVE_ANALOG_WAKE_PERIOD_SELECT ||
      sys_ll_get_ana_reg12_clk_sel() != s_analog_clock_before || sys_ll_get_ana_reg9_spi_latch1v())
    s_native_faulted = true;
  unhygienix_native_sleep_stage(T5AISleepStage_CP_AnalogWakePrepared);
}

static void prv_analog_wake_restore(void) {
  if (!s_analog_owned)
    return;
  if (sys_ll_get_ana_reg9_spi_latch1v() ||
      sys_ll_get_ana_reg10_timer_sel() != NATIVE_ANALOG_WAKE_PERIOD_SELECT ||
      sys_ll_get_ana_reg12_clk_sel() != s_analog_clock_before) {
    s_native_faulted = true;
    return; // Preserve an unexpected other-owner change.
  }
  sys_ll_set_ana_reg9_spi_latch1v(1u);
  sys_ll_set_ana_reg10_spi_timerwken(0u);
  sys_ll_set_ana_reg10_timer_sel(s_analog_period_before);
  sys_ll_set_ana_reg9_spi_latch1v(0u);
  if (sys_ll_get_ana_reg10_spi_timerwken() ||
      sys_ll_get_ana_reg10_timer_sel() != s_analog_period_before ||
      sys_ll_get_ana_reg12_clk_sel() != s_analog_clock_before)
    s_native_faulted = true;
  s_analog_owned = false;
  unhygienix_native_sleep_stage(T5AISleepStage_CP_AnalogWakeRestored);
}

static uint32_t prv_analog_wake_state(void) {
  return (sys_ll_get_ana_reg10_spi_timerwken() ? T5AI_SLEEP_ANALOG_STATE_ENABLED : 0u) |
         (sys_ll_get_ana_reg10_timer_sel() << T5AI_SLEEP_ANALOG_STATE_PERIOD_SHIFT) |
         (sys_ll_get_ana_reg12_clk_sel() << T5AI_SLEEP_ANALOG_STATE_CLOCK_SHIFT) |
         (sys_ll_get_ana_reg9_spi_latch1v() << T5AI_SLEEP_ANALOG_STATE_LATCH_SHIFT);
}

// CPU2 is held in reset and powered down by the readiness contract.
__attribute__((section(".itcm_sec_code"))) uint32_t
unhygienix_native_sleep_condition(uint32_t value) {
  if (!unhygienix_native_sleep_prepared)
    return value;
  const uint32_t flash_condition = s_native_vendor_flash_idle ? 0u : NATIVE_SLEEP_NEEDS_FLASH_IDLE;
  const uint32_t cp_condition = s_native_vendor_cpu_idle ? 0u : NATIVE_SLEEP_NEEDS_CP_WFI;
  const uint32_t ap_condition = s_native_vendor_subcores_on ? 0u : NATIVE_SLEEP_NEEDS_AP_WFI;
  return (value & ~NATIVE_SLEEP_CONDITION_MASK) | flash_condition | cp_condition | ap_condition |
         NATIVE_SLEEP_GLOBAL_ENABLE;
}

__attribute__((section(".itcm_sec_code"))) static void prv_native_sleep_condition_restore(void) {
  if (!s_sleep_condition_saved)
    return;
  const uint32_t current = sys_ll_get_cpu_power_sleep_wakeup_value();
  sys_ll_set_cpu_power_sleep_wakeup_value((current & ~NATIVE_SLEEP_CONDITION_MASK) |
                                          s_sleep_condition_before);
  if ((sys_ll_get_cpu_power_sleep_wakeup_value() & NATIVE_SLEEP_CONDITION_MASK) !=
      s_sleep_condition_before)
    s_native_faulted = true;
  // The native lease owns R40: delay and halt policy must not leak into normal idle.
  aon_pmu_ll_set_r40(s_pmu_power_parameter_before);
  if (aon_pmu_ll_get_r40() != s_pmu_power_parameter_before)
    s_native_faulted = true;
  s_sleep_condition_saved = false;
}

__attribute__((section(".itcm_sec_code"))) uint32_t
unhygienix_native_sleep_power_domains(uint32_t value) {
  return unhygienix_native_sleep_prepared ? value & ~T5AI_SLEEP_AP_RETAINED_DOMAINS : value;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_cp_gpio_owned(void) {
  // Native watch GPIO configuration and interrupt acknowledgement belong to AP.
  return !unhygienix_native_sleep_prepared;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_mask_required(void) {
  // Native entry defers this transition until the final WFI admission.
  return !unhygienix_native_sleep_prepared;
}

__attribute__((section(".itcm_sec_code"))) void unhygienix_native_sleep_mask_enter(void) {
  if (unhygienix_native_sleep_prepared && !s_mask_saved) {
    s_mask_before = sys_ll_get_cpu0_int_halt_clk_op_cpu0_int_mask();
    s_mask_saved = true;
  }
}

__attribute__((section(".itcm_sec_code"))) void unhygienix_native_sleep_mask_exit(void) {
  prv_native_sleep_condition_restore();
  if (unhygienix_native_sleep_prepared && s_mask_saved) {
    sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask(s_mask_before);
    s_mask_after = sys_ll_get_cpu0_int_halt_clk_op_cpu0_int_mask();
    if (s_mask_after != s_mask_before)
      s_native_faulted = true;
    s_mask_saved = false;
  }
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_retain_modules(void) {
  return unhygienix_native_sleep_prepared && s_native_retain_modules;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_domain_bypass(void) {
  return unhygienix_native_sleep_prepared && s_native_domain_bypass;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_bsp_restore(void) {
  return unhygienix_native_sleep_prepared && s_native_bsp_restore;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_flash_restore(void) {
  return unhygienix_native_sleep_prepared && s_native_flash_restore;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_core_only(void) {
  return unhygienix_native_sleep_prepared && s_native_core_only;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_mask_only(void) {
  return unhygienix_native_sleep_prepared && s_native_mask_only;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_sleep_keep_hf(void) {
  return unhygienix_native_sleep_prepared && s_native_keep_hf;
}

#define NATIVE_RTC_CONTROL (*(volatile uint32_t *)0x44000200u)

__attribute__((section(".itcm_sec_code"))) static uint32_t prv_native_sleep_control_read(void) {
  return SCB->SCR;
}

#define NATIVE_TRACE_DRAIN_BUDGET_MS    2u
#define NATIVE_TRACE_DRAIN_BUDGET_TICKS (NATIVE_TRACE_DRAIN_BUDGET_MS * NATIVE_AON_TICKS_PER_MS)
#define NATIVE_TRACE_DRAIN_MAX_POLLS    65536u

// The trace must leave the UART before the final IRQ/deadline checks and mask.
__attribute__((section(".itcm_sec_code"))) static bool prv_native_trace_admission(void) {
  if (!s_trace_uart_owned || !s_native_vendor_subcores_on || !s_native_keep_hf)
    return true;
  prv_native_trace_word(T5AISleepSnapshot_SleepConfig, sys_ll_get_cpu_power_sleep_wakeup_value());
  prv_native_trace_word(T5AISleepSnapshot_WakeConfig, aon_pmu_ll_get_r41());
  prv_native_trace_word(T5AISleepSnapshot_PowerParameter, aon_pmu_ll_get_r40());
  prv_native_trace_word(T5AISleepSnapshot_CPUStatus, sys_ll_get_cpu_current_run_status_value());
  prv_native_trace_word(T5AISleepSnapshot_HFBeforeMask, sys_ll_get_ana_reg5_value());
  prv_native_trace_word(T5AISleepSnapshot_CPUSleepControl, prv_native_sleep_control_read());
  prv_native_trace_word(T5AISleepSnapshot_CPCacheRetention, sys_ll_get_cpu0_lv_sleep_cfg_value());
  prv_native_trace_word(T5AISleepSnapshot_MemoryRetention, sys_ll_get_cpu_device_mem_ctrl3_value());
  prv_native_trace_word(T5AISleepSnapshot_RTCControl, NATIVE_RTC_CONTROL);
  prv_native_trace_uart(T5AISleepStage_CP_AdmissionSnapshot);
  const uint32_t start = NATIVE_AON_COUNT_LOW;
  for (unsigned poll = 0; poll < NATIVE_TRACE_DRAIN_MAX_POLLS; ++poll) {
    if (NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_STATUS_OFFSET) & NATIVE_TRACE_UART_TX_EMPTY)
      return true;
    if ((uint32_t)(NATIVE_AON_COUNT_LOW - start) >= NATIVE_TRACE_DRAIN_BUDGET_TICKS)
      return false;
  }
  return false;
}

__attribute__((section(".itcm_sec_code"))) void unhygienix_native_sleep_wait(void) {
  uint32_t sleep_control_before = 0u;
  if (unhygienix_native_sleep_prepared) {
    sleep_control_before = SCB->SCR;
    const uint32_t expected_sleep_control = s_native_shallow_wfi
                                                ? sleep_control_before & ~SCB_SCR_SLEEPDEEP_Msk
                                                : sleep_control_before | SCB_SCR_SLEEPDEEP_Msk;
    SCB->SCR = expected_sleep_control;
    __DSB();
    __ISB();
    const bool architecture_ready = (prv_native_sleep_control_read() & SCB_SCR_SLEEPDEEP_Msk) ==
                                    (expected_sleep_control & SCB_SCR_SLEEPDEEP_Msk);
    const bool trace_drained = prv_native_trace_admission();
    s_final_lead = s_native_deadline - NATIVE_AON_COUNT_LOW;
    s_sleep_config = sys_ll_get_cpu_power_sleep_wakeup_value();
    s_wake_config = aon_pmu_ll_get_r41();
    s_power_parameter = aon_pmu_ll_get_r40();
    s_wake_status_before = aon_pmu_ll_get_r71();
    s_wake_clear_before = aon_pmu_ll_get_r43();
    s_clock_mode_low = sys_ll_get_cpu_clk_div_mode1_value();
    s_hf_low = sys_ll_get_ana_reg5_value();
    s_final_pending_low = sys_ll_get_cpu0_int_0_31_status_value();
    s_final_pending_high = sys_ll_get_cpu0_int_32_63_status_value();
    s_final_nvic_low = NVIC->ISPR[0];
    s_final_nvic_high = NVIC->ISPR[1];
    s_mask_at_final = sys_ll_get_cpu0_int_halt_clk_op_cpu0_int_mask();
    s_final_ap_pending_low = sys_ll_get_cpu1_int_0_31_status_value();
    s_final_ap_pending_high = sys_ll_get_cpu1_int_32_63_status_value();
    s_final_gpio_status_low = NATIVE_GPIO_STATUS_LOW;
    s_final_gpio_status_high = NATIVE_GPIO_STATUS_HIGH;
    ++s_clock_count;
    if (s_native_clocks_only) {
      SCB->SCR = sleep_control_before;
      __DSB();
      return;
    }
    const bool analog_ready =
        !s_native_analog_wake ||
        (s_analog_owned && sys_ll_get_ana_reg10_spi_timerwken() &&
         sys_ll_get_ana_reg10_timer_sel() == NATIVE_ANALOG_WAKE_PERIOD_SELECT &&
         sys_ll_get_ana_reg12_clk_sel() == s_analog_clock_before &&
         !sys_ll_get_ana_reg9_spi_latch1v() &&
         (uint32_t)(NATIVE_AON_COUNT_LOW - s_analog_start) < NATIVE_ANALOG_WAKE_ENTRY_BUDGET_TICKS);
    s_final_reject =
        (analog_ready ? 0u : T5AI_SLEEP_FINAL_REJECT_ANALOG_WAKE) |
        (architecture_ready ? 0u : T5AI_SLEEP_FINAL_REJECT_ARCH) |
        ((int32_t)s_final_lead < (int32_t)T5AI_SLEEP_CP_MIN_LEAD_TICKS || !s_native_armed ||
                 !s_native_permit
             ? T5AI_SLEEP_FINAL_REJECT_LEAD
             : 0u) |
        (s_final_pending_low || s_final_pending_high || s_final_nvic_low || s_final_nvic_high
             ? T5AI_SLEEP_FINAL_REJECT_IRQ
             : 0u) |
        ((SCB->ICSR & NATIVE_SYSTICK_PENDING_MASK) ? T5AI_SLEEP_FINAL_REJECT_TICK : 0u) |
        (s_mask_at_final ? T5AI_SLEEP_FINAL_REJECT_MASK : 0u) |
        // Raw GPIO status remains visible when AP wake routes are suspended.
        (s_final_ap_pending_low || s_final_ap_pending_high || s_final_gpio_status_low ||
                 s_final_gpio_status_high
             ? T5AI_SLEEP_FINAL_REJECT_AP_IRQ
             : 0u) |
        (!trace_drained || (s_trace_uart_owned &&
                            !(NATIVE_TRACE_UART_REGISTER(NATIVE_TRACE_UART_STATUS_OFFSET) &
                              NATIVE_TRACE_UART_TX_EMPTY))
             ? T5AI_SLEEP_FINAL_REJECT_UART
             : 0u) |
        ((NATIVE_FLASH_OPERATION_CONTROL & NATIVE_FLASH_BUSY) || !unhygienix_native_flash_ready()
             ? T5AI_SLEEP_FINAL_REJECT_FLASH
             : 0u);
    if (s_final_reject) {
      ++s_final_skips;
      SCB->SCR = sleep_control_before;
      __DSB();
      return;
    }
    if (s_wake_clear_before & NATIVE_PMU_WAKE_CLEAR_ASSERTED)
      ++s_pmu_clear_held_count;
    // Finish the cache-cleaning breadcrumb before the hardware can admit sleep.
    if (!s_native_mask_barrier)
      ++unhygienix_native_lowvol_count;
    unhygienix_native_sleep_stage(s_native_mask_barrier ? T5AISleepStage_CP_MaskBarrierEnter
                                                        : T5AISleepStage_CP_WFI);
  }
  if (unhygienix_native_sleep_prepared && !s_native_no_hw_mask) {
    unhygienix_native_sleep_mask_enter();
    const uint32_t control = sys_ll_get_cpu0_int_halt_clk_op_value();
    if (s_native_persistent_trace) {
      const uint32_t word = NATIVE_RESET_REASON_WORD;
      if (!s_persistent_recording || prv_persistent_reason(word) != s_persistent_reason_last ||
          s_native_faulted) {
        s_final_reject |= T5AI_SLEEP_FINAL_REJECT_TRACE;
        ++s_final_skips;
        SCB->SCR = sleep_control_before;
        __DSB();
        return;
      }
    }
    s_native_system_mask_used = true;
    if (s_native_mask_barrier) {
      unhygienix_cp_mask_barrier((volatile uint32_t *)NATIVE_CP_CONTROL_ADDRESS,
                                 control | NATIVE_CP_INTERRUPT_MASK, control);
      ++s_mask_barrier_count;
      s_mask_barrier_returned = true;
    } else if (s_native_persistent_trace) {
      s_persistent_reason_last = (T5AI_SLEEP_CP_TRACE_REASON_BASE | T5AISleepStage_CP_Woke)
                                 << NATIVE_CP_REASON_SHIFT;
      unhygienix_cp_masked_wfi_trace((volatile uint32_t *)NATIVE_CP_CONTROL_ADDRESS,
                                     control | NATIVE_CP_INTERRUPT_MASK, s_persistent_reason_last);
    } else {
      unhygienix_cp_masked_wfi((volatile uint32_t *)NATIVE_CP_CONTROL_ADDRESS,
                               control | NATIVE_CP_INTERRUPT_MASK);
    }
  } else {
    arch_deep_sleep();
  }
  s_mask_after_wfi = sys_ll_get_cpu0_int_halt_clk_op_cpu0_int_mask();
  if (unhygienix_native_sleep_prepared) {
    SCB->SCR = sleep_control_before;
    __DSB();
  }
  s_wake_status_after = aon_pmu_ll_get_r71();
  s_wake_clear_after = aon_pmu_ll_get_r43();
  unhygienix_native_sleep_stage(s_native_mask_barrier ? T5AISleepStage_CP_MaskBarrierReturned
                                                      : T5AISleepStage_CP_Woke);
  if (unhygienix_native_sleep_prepared) {
    if (!unhygienix_native_flash_resume()) {
      s_native_faulted = true;
      unhygienix_native_sleep_stage(T5AISleepStage_CP_FlashResumeFailed);
      // Keep the failing wake in ITCM until the independent watchdog guard resets it.
      for (;;)
        __asm volatile("nop");
    }
    unhygienix_native_sleep_stage(T5AISleepStage_CP_FlashControllerRestored);
  }
}

static uint32_t prv_native_state(void) {
  uint32_t state = T5AI_SLEEP_STATE_GPIO_RETAINED;
  if (unhygienix_native_peripherals_retained())
    state |= T5AI_SLEEP_STATE_PERIPHERALS_RETAINED;
  // The pinned start/stop sequence releases reset with 1 and asserts it with 0.
  if (sys_ll_get_cpu2_int_halt_clk_op_cpu2_sw_rst() == NATIVE_CPU_RESET_ASSERTED)
    state |= T5AI_SLEEP_STATE_SIBLING_RESET_REQUEST;
  if (sys_ll_get_cpu2_int_halt_clk_op_cpu2_pwr_dw())
    state |= T5AI_SLEEP_STATE_SIBLING_POWER_DOWN;
  if (sys_ll_get_cpu_current_run_status_cpu2_sw_reset() == NATIVE_CPU_RESET_ASSERTED)
    state |= T5AI_SLEEP_STATE_SIBLING_RESET_ACK;
  if (sys_ll_get_cpu_current_run_status_cpu2_pwr_dw_state() == NATIVE_CPU_POWERED_DOWN)
    state |= T5AI_SLEEP_STATE_SIBLING_POWER_ACK;
#if !CONFIG_PSRAM_POWER_DOMAIN_LV_DISABLE && !CONFIG_AHBP_POWER_DOMAIN_LV_DISABLE
  if (unhygienix_native_memory_retained())
    state |= T5AI_SLEEP_STATE_MEMORY_RETAINED;
#endif
#if CONFIG_AON_WDT && !CONFIG_INT_AON_WDT && CONFIG_NMI_WDT_EN
  state |= T5AI_SLEEP_STATE_WATCHDOG_GUARD;
#endif
  if (unhygienix_native_sleep_prepared)
    state |= T5AI_SLEEP_STATE_PREPARED;
  if (aon_pmu_ll_get_r3_cp1_enter_wfi_state())
    state |= T5AI_SLEEP_STATE_AP_PUBLISHED;
  if (unhygienix_native_lowvol_count)
    state |= T5AI_SLEEP_STATE_LOWVOLT_ENTERED;
  return state;
}

static void prv_native_vote(bool allow) {
  if (s_native_app_released == allow)
    return;
  if (bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP,
                                   allow ? NATIVE_APP_SLEEP_ALLOWED : NATIVE_APP_AWAKE,
                                   0u) != BK_OK) {
    s_native_faulted = true;
    s_native_permit = false;
  } else
    s_native_app_released = allow;
}

static void prv_native_revoke_ready(void) {
  // The flash bus and callbacks may still be suspended even before HAL WFI.
  if (!s_native_ready_flag || s_resources_active)
    return;
  aon_pmu_ll_set_r3_cp0_sleep_vote_state(0u);
  s_native_ready_flag = false;
}

// CP owns the ready bit until AP has published its WFI bit. This serializes the
// two cores' RMW access to AON R3 without pretending that a core is already asleep.
static uint32_t prv_native_admission_tick(void) {
  return NATIVE_AON_COUNT_LOW;
}

static bool prv_native_wait_ap_publication(void) {
  if (aon_pmu_ll_get_r3_cp1_enter_wfi_state())
    return true;
  if (!unhygienix_native_sleep_prepared || !s_native_armed || !s_native_permit ||
      s_native_faulted || s_native_wake_only)
    return false;
  const uint32_t start = prv_native_admission_tick();
  while (!aon_pmu_ll_get_r3_cp1_enter_wfi_state()) {
    const uint32_t now = prv_native_admission_tick();
    if ((uint32_t)(now - start) >= NATIVE_AP_PUBLICATION_WAIT_TICKS ||
        (int32_t)(s_native_deadline - now) < (int32_t)T5AI_SLEEP_CP_MIN_LEAD_TICKS ||
        sys_ll_get_cpu0_int_0_31_status_value() || sys_ll_get_cpu0_int_32_63_status_value() ||
        NVIC->ISPR[0] || NVIC->ISPR[1] || (SCB->ICSR & NATIVE_SYSTICK_PENDING_MASK))
      return false;
  }
  return true;
}

bool unhygienix_native_sleep_subcores_ready(void) {
  if (!prv_native_wait_ap_publication())
    return false;
  const uint32_t state = prv_native_state();
  if (!s_native_armed || s_native_faulted || !s_native_permit ||
      (state & T5AI_SLEEP_REQUIRED_STATE) != T5AI_SLEEP_REQUIRED_STATE) {
    s_native_permit = false;
    prv_native_vote(false);
    prv_native_revoke_ready();
    return false;
  }
  // Let pending physical ACKs and every logical sender drain before suspending CP.
  if (!unhygienix_native_mailbox_sleep_ready())
    return false;
  // Bounded AP wake control: retain the ready lease without allowing CP low voltage.
  if (s_native_wake_only)
    return false;
  prv_native_vote(true);
  if (!s_native_faulted) {
    const uint64_t missing = unhygienix_native_sleep_missing_votes();
    s_native_missing_low = (uint32_t)missing;
    s_native_missing_high = (uint32_t)(missing >> 32);
    ++s_native_ready_samples;
  }
  return !s_native_faulted;
}

// A closed lease still needs queued frames delivered before the next idle entry.
bool unhygienix_native_sleep_tickless_allowed(void) {
  if (s_native_cleanup_pending || s_native_generation != 0u) {
    const uint32_t mailbox_irq = (uint32_t)INT_SRC_MAILBOX;
    const uint32_t control = NATIVE_MAILBOX_CP_CONTROL;
    const bool core_enabled = !__get_PRIMASK() && !__get_BASEPRI() && !__get_FAULTMASK();
    const bool route_enabled = !sys_ll_get_cpu0_int_halt_clk_op_cpu0_int_mask() &&
                               (sys_ll_get_cpu0_int_32_63_en_value() & NATIVE_MAILBOX_CP_ROUTE) &&
                               (control & NATIVE_MAILBOX_INTERRUPT_ENABLE);
    const uint32_t state =
        (core_enabled ? NATIVE_CLEANUP_CORE_ENABLED : 0u) |
        (NVIC_GetEnableIRQ(mailbox_irq) ? NATIVE_CLEANUP_NVIC_ENABLED : 0u) |
        ((control & NATIVE_MAILBOX_FIFO_NOT_EMPTY) ? NATIVE_CLEANUP_FIFO_QUEUED : 0u) |
        (route_enabled ? NATIVE_CLEANUP_ROUTE_ENABLED : 0u) |
        (NVIC_GetPendingIRQ(mailbox_irq) ? NATIVE_CLEANUP_IRQ_PENDING : 0u);
    const bool queued = (state & NATIVE_CLEANUP_FIFO_QUEUED) != 0u;
    const bool restored_awake = s_native_system_mask_used && !unhygienix_native_sleep_prepared;
    if ((s_native_cleanup_pending || queued || restored_awake) &&
        s_cleanup_idle_refusals != UINT32_MAX)
      ++s_cleanup_idle_refusals;
    if (restored_awake && !s_native_cleanup_pending && s_native_normal_sleep_skips != UINT32_MAX)
      ++s_native_normal_sleep_skips;
    s_cleanup_irq_state = state;
    prv_cleanup_delivery_snapshot();
    if ((state & NATIVE_CLEANUP_DELIVERY_READY) == NATIVE_CLEANUP_DELIVERY_READY &&
        !(state & NATIVE_CLEANUP_IRQ_PENDING)) {
      // Preserve the existing ISR's FIFO and ACK ownership after route restoration.
      if (s_cleanup_irq_kicks != UINT32_MAX)
        ++s_cleanup_irq_kicks;
      NVIC_SetPendingIRQ(mailbox_irq);
      __DSB();
      __ISB();
    }
    // Refuse before the caller stops SysTick outside a coordinated sleep window.
    return !s_native_cleanup_pending && !queued && !restored_awake;
  }
  return true;
}

void unhygienix_native_sleep_resumed(void) {
  prv_native_sleep_condition_restore();
  if (!unhygienix_native_sleep_prepared)
    return;
  prv_analog_wake_restore();
  if (s_analog_owned) {
    s_native_permit = false;
    prv_native_vote(false);
    return; // Keep AP held for its independent reset guard on failed rollback.
  }
  // Observe callback-complete state before AP release; no register writes here.
  s_pmu_power_parameter_after = aon_pmu_ll_get_r40();
  s_irq_low_after = sys_ll_get_cpu0_int_0_31_en_value();
  s_irq_high_after = sys_ll_get_cpu0_int_32_63_en_value();
  s_peripheral_clocks_after = sys_ll_get_cpu_device_clk_enable_value();
  unhygienix_native_sleep_stage(T5AISleepStage_CP_Restored);
  s_native_permit = false;
  prv_native_vote(false);
  unhygienix_native_sleep_stage(T5AISleepStage_CP_AppVetoRestored);
  // The tickless wrapper still owns its outer critical section and tick restore.
  s_native_resume_pending = true;
}

void unhygienix_native_sleep_interrupts_restored(void) {
  if (!s_native_resume_pending || !unhygienix_native_sleep_prepared || __get_PRIMASK() ||
      __get_BASEPRI())
    return;
  s_native_resume_pending = false;
  s_native_cleanup_pending = true;
  prv_native_revoke_ready();
  unhygienix_native_sleep_stage(T5AISleepStage_CP_ReadyRevoked);
  prv_native_trace_snapshot();
  // The sleeping AP must not depend on a deferred CP worker to ring its wake doorbell.
  const uint32_t tick = (uint32_t)bk_aon_rtc_get_current_tick(AON_RTC_ID_1);
  if (unhygienix_native_sleep_notify_ap(s_native_generation, tick) != BK_OK)
    s_native_faulted = true;
  unhygienix_native_sleep_stage(T5AISleepStage_CP_ResumeSent);
  unhygienix_native_sleep_stage(T5AISleepStage_CP_InterruptsReleased);
}

static void unhygienix_alarm_callback(aon_rtc_id_t id, uint8_t *name, void *context) {
  (void)name;
  const uint32_t generation = (uint32_t)(uintptr_t)context;
  if (!s_native_armed || generation != s_native_generation)
    return;
  s_native_armed = false;
  s_native_permit = false;
  // The CP idle/resource-restore path reapplies the veto outside the RTC ISR.
  s_native_fired = generation;
  const low_pwr_core_msg_t event = {
    .event = LOW_PWR_CORE_UNHYGIENIX_ALARM_FIRED,
    .param1 = generation,
    .param2 = (uint32_t)bk_aon_rtc_get_current_tick(id),
  };
  // No mailbox retries or RTOS delay from the RTC ISR.
  low_pwr_core_msg_t queued = event;
  if (bk_low_pwr_core_send_msg(&queued) != BK_OK)
    s_native_faulted = true;
}

static void unhygienix_alarm_fired(const low_pwr_core_msg_t *message) {
  if (message->param1 != s_native_generation || s_native_cancelled ||
      s_native_fired != message->param1)
    return;
  s_native_fired = 0u;
  if (bk_pm_cp0_response_cp1(T5AI_PM_NATIVE_ALARM_WAKE, message->param1, message->param2,
                             T5AI_PM_NATIVE_ALARM_ABI) != BK_OK)
    s_native_faulted = true;
}

static void unhygienix_sleep_resumed_event(const low_pwr_core_msg_t *message) {
  if (message->param1 != s_native_generation || s_native_cancelled)
    return;
  if (bk_pm_cp0_response_cp1(T5AI_PM_NATIVE_SLEEP_RESUMED, message->param1, message->param2,
                             T5AI_PM_NATIVE_ALARM_ABI) != BK_OK)
    s_native_faulted = true;
}

static void unhygienix_alarm_request(const low_pwr_core_msg_t *message) {
  const uint32_t generation = message->param1;
  const uint32_t ticks = message->param2;
  const T5AIAlarmOperation operation = message->param3;
  if (operation == T5AIAlarm_Cancel) {
    s_cleanup_cancel_progress = T5AICancelProgress_Received;
    unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_NativeRequest);
  }
  const bool analog_request = operation == T5AIAlarm_ArmSleepAnalogWake ||
                              operation == T5AIAlarm_ArmSleepAnalogRetainDomains ||
                              operation == T5AIAlarm_ArmSleepAnalogShallowWFI ||
                              operation == T5AIAlarm_ArmSleepMaskBarrier;
  const bool sleep_request =
      operation == T5AIAlarm_ArmSleep || operation == T5AIAlarm_ArmSleepWakeOnly ||
      operation == T5AIAlarm_ArmSleepResourcesOnly || operation == T5AIAlarm_ArmSleepClocksOnly ||
      operation == T5AIAlarm_ArmSleepClocksLive || operation == T5AIAlarm_ArmSleepCoreOnly ||
      operation == T5AIAlarm_ArmSleepFlashRestore || operation == T5AIAlarm_ArmSleepBspRestore ||
      operation == T5AIAlarm_ArmSleepDomainBypass || operation == T5AIAlarm_ArmSleepRetainModules ||
      operation == T5AIAlarm_ArmSleepKeepHF || operation == T5AIAlarm_ArmSleepNoHardwareMask ||
      operation == T5AIAlarm_ArmSleepNoHardwareMaskClocksOff ||
      operation == T5AIAlarm_ArmSleepVendorFlashIdle ||
      operation == T5AIAlarm_ArmSleepVendorCPUIdle ||
      operation == T5AIAlarm_ArmSleepVendorSubcoresOn ||
      operation == T5AIAlarm_ArmSleepVendorMasked ||
      operation == T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF ||
      operation == T5AIAlarm_ArmSleepVendorMaskedKeepHF ||
      operation == T5AIAlarm_ArmSleepMaskedCPWFIKeepHF ||
      operation == T5AIAlarm_ArmSleepVendorLPOHalt ||
      operation == T5AIAlarm_ArmSleepPersistentTrace || operation == T5AIAlarm_ArmSleepMaskOnly ||
      analog_request;
  bk_err_t status = BK_FAIL;
  if (!s_trace_captured) {
    if (s_native_trace.magic == T5AI_SLEEP_TRACE_MAGIC &&
        s_native_trace.stage == ~s_native_trace.inverse) {
      s_previous_stage = s_native_trace.stage;
      s_previous_generation = s_native_trace.generation;
    }
    s_trace_captured = true;
  }
  uint32_t reply_generation = generation;
  if (operation == T5AIAlarm_Stage || operation == T5AIAlarm_PreviousStage ||
      operation == T5AIAlarm_PreviousGeneration) {
    const bool valid = s_native_trace.magic == T5AI_SLEEP_TRACE_MAGIC &&
                       s_native_trace.stage == ~s_native_trace.inverse;
    reply_generation = operation == T5AIAlarm_Stage
                           ? (valid ? s_native_trace.stage : T5AISleepStage_None)
                       : operation == T5AIAlarm_PreviousStage ? s_previous_stage
                                                              : s_previous_generation;
    status = BK_OK;
  } else if (operation == T5AIAlarm_AnalogPrepareLeadTicks ||
             operation == T5AIAlarm_AnalogPrepareReject) {
    reply_generation = operation == T5AIAlarm_AnalogPrepareLeadTicks ? s_analog_prepare_lead
                                                                     : s_analog_prepare_reject;
    status = BK_OK;
  } else if (operation == T5AIAlarm_AnalogWakeState) {
    reply_generation = prv_analog_wake_state();
    status = BK_OK;
  } else if (operation == T5AIAlarm_Query) {
    reply_generation = s_native_generation;
    status = s_native_faulted ? BK_FAIL : BK_OK;
  } else if (operation >= T5AIAlarm_ClockCount && operation <= T5AIAlarm_FinalNvicHigh) {
    const uint32_t diagnostics[] = {
      s_clock_count, s_final_lead,        s_sleep_config,       s_wake_config,    s_clock_mode_low,
      s_hf_low,      s_final_pending_low, s_final_pending_high, s_final_nvic_low, s_final_nvic_high,
    };
    reply_generation = diagnostics[operation - T5AIAlarm_ClockCount];
    status = BK_OK;
  } else if (operation >= T5AIAlarm_FinalReject && operation <= T5AIAlarm_MaskAtFinal) {
    const uint32_t diagnostics[] = {
      s_final_reject, s_final_skips, s_mask_before, s_mask_after, s_mask_at_final,
    };
    reply_generation = diagnostics[operation - T5AIAlarm_FinalReject];
    status = BK_OK;
  } else if (operation == T5AIAlarm_FinalAPPendingLow ||
             operation == T5AIAlarm_FinalAPPendingHigh) {
    reply_generation =
        operation == T5AIAlarm_FinalAPPendingLow ? s_final_ap_pending_low : s_final_ap_pending_high;
    status = BK_OK;
  } else if (operation == T5AIAlarm_FinalGPIOStatusLow ||
             operation == T5AIAlarm_FinalGPIOStatusHigh) {
    reply_generation = operation == T5AIAlarm_FinalGPIOStatusLow ? s_final_gpio_status_low
                                                                 : s_final_gpio_status_high;
    status = BK_OK;
  } else if (operation >= T5AIAlarm_PowerParameter && operation <= T5AIAlarm_WakeStatusAfter) {
    const uint32_t diagnostics[] = {s_power_parameter, s_wake_status_before, s_wake_status_after};
    reply_generation = diagnostics[operation - T5AIAlarm_PowerParameter];
    status = BK_OK;
  } else if (operation >= T5AIAlarm_PMUWakeCount && operation <= T5AIAlarm_HardwareMaskAfterWFI) {
    const uint32_t diagnostics[] = {
      s_pmu_wake_count,    s_pmu_wake_sources, s_pmu_wake_ticks,       s_pmu_ambiguous_count,
      s_wake_clear_before, s_wake_clear_after, s_pmu_clear_held_count, s_mask_after_wfi,
    };
    reply_generation = diagnostics[operation - T5AIAlarm_PMUWakeCount];
    status = BK_OK;
  } else if (operation >= T5AIAlarm_BootPMUWake && operation <= T5AIAlarm_BootPMUSnapshotValid) {
    const uint32_t diagnostics[] = {
      s_boot_pmu_wake, s_boot_pmu_reset, s_boot_pmu_power, s_boot_pmu_wake_config,
      s_boot_pmu_captured
    };
    reply_generation = diagnostics[operation - T5AIAlarm_BootPMUWake];
    status = BK_OK;
  } else if (operation >= T5AIAlarm_PMUPolicyBefore &&
             operation <= T5AIAlarm_PeripheralClocksAfter) {
    const uint32_t snapshots[] = {
      s_pmu_power_parameter_before,
      s_pmu_power_parameter_after,
      s_irq_low_before,
      s_irq_low_after,
      s_irq_high_before,
      s_irq_high_after,
      s_peripheral_clocks_before,
      s_peripheral_clocks_after,
    };
    reply_generation = snapshots[operation - T5AIAlarm_PMUPolicyBefore];
    status = BK_OK;
  } else if (operation == T5AIAlarm_NormalSleepSkips) {
    status = BK_OK;
    reply_generation = s_native_normal_sleep_skips;
  } else if (operation == T5AIAlarm_CleanupMailboxProgress) {
    status = BK_OK;
    reply_generation = s_cleanup_mailbox_progress;
  } else if (operation == T5AIAlarm_CleanupIRQDelivery) {
    status = BK_OK;
    reply_generation = s_cleanup_irq_delivery;
  } else if (operation == T5AIAlarm_CleanupMailboxISRs) {
    status = BK_OK;
    reply_generation = s_cleanup_mailbox_isrs;
  } else if (operation == T5AIAlarm_CleanupMailboxEmptyStatus) {
    status = BK_OK;
    reply_generation = s_cleanup_mailbox_empty_status;
  } else if (operation == T5AIAlarm_CleanupIRQState) {
    status = BK_OK;
    reply_generation = s_cleanup_irq_state;
  } else if (operation == T5AIAlarm_CleanupIRQKicks) {
    status = BK_OK;
    reply_generation = s_cleanup_irq_kicks;
  } else if (operation == T5AIAlarm_CleanupIdleRefusals) {
    status = BK_OK;
    reply_generation = s_cleanup_idle_refusals;
  } else if (operation == T5AIAlarm_MaskBarrierCount) {
    status = BK_OK;
    reply_generation = s_mask_barrier_count;
  } else if (operation == T5AIAlarm_ResourceCount) {
    reply_generation = s_resources_count;
    status = BK_OK;
  } else if (operation == T5AIAlarm_State || operation == T5AIAlarm_LowVoltageCount) {
    reply_generation =
        operation == T5AIAlarm_State ? prv_native_state() : unhygienix_native_lowvol_count;
    status = s_native_faulted ? BK_FAIL : BK_OK;
  } else if (operation == T5AIAlarm_MissingVotesLow || operation == T5AIAlarm_MissingVotesHigh ||
             operation == T5AIAlarm_ReadySamples) {
    reply_generation = operation == T5AIAlarm_MissingVotesLow    ? s_native_missing_low
                       : operation == T5AIAlarm_MissingVotesHigh ? s_native_missing_high
                                                                 : s_native_ready_samples;
    status = s_native_faulted ? BK_FAIL : BK_OK;
  } else if (operation >= T5AIAlarm_HalReject && operation <= T5AIAlarm_LowVoltageTicks) {
    const uint32_t diagnostics[] = {s_hal_reject,     s_hal_pending_low, s_hal_pending_high,
                                    s_hal_status_low, s_hal_status_high, s_prepare_ticks,
                                    s_lowvol_ticks};
    reply_generation = diagnostics[operation - T5AIAlarm_HalReject];
    status = BK_OK;
  } else if (operation == T5AIAlarm_FinishCleanup && generation != 0u &&
             generation == s_native_generation && s_native_cancelled &&
             !unhygienix_native_sleep_prepared && !s_resources_active && !s_analog_owned &&
             !s_native_resume_pending && !s_native_app_released && !s_lpo_saved) {
    // AP has finished its post-wake validation; retire only after this reply's ACK.
    const uint32_t finish_irq_state = rtos_disable_int();
    if (s_native_cleanup_pending) {
      s_cleanup_reply_generation = generation;
      s_cleanup_reply_pending = true;
    }
    status = s_native_faulted ? BK_FAIL : BK_OK;
    rtos_enable_int(finish_irq_state);
  } else if (!s_native_faulted && (operation == T5AIAlarm_Arm || sleep_request) &&
             generation > s_native_generation && !s_native_cleanup_pending && !s_native_armed &&
             s_native_fired == 0u && ticks >= T5AI_PM_NATIVE_ALARM_MIN_TICKS &&
             ticks <= T5AI_PM_NATIVE_ALARM_MAX_TICKS &&
             (!analog_request ||
              (ticks >= NATIVE_ANALOG_WAKE_MIN_TICKS && !sys_ll_get_ana_reg10_spi_timerwken() &&
               !sys_ll_get_ana_reg9_spi_latch1v())) &&
             (!sleep_request ||
              ((prv_native_state() & (T5AI_SLEEP_REQUIRED_STATE | T5AI_SLEEP_STATE_AP_PUBLISHED)) ==
               T5AI_SLEEP_REQUIRED_STATE))) {
    const uint32_t irq_state = rtos_disable_int();
    s_native_generation = generation;
    s_native_cancelled = false;
    s_native_cleanup_pending = false;
    s_cleanup_irq_state = 0u;
    s_cleanup_irq_delivery = 0u;
    s_cleanup_mailbox_progress = T5AIMailboxProgress_None;
    s_cleanup_cancel_progress = T5AICancelProgress_None;
    s_cleanup_reply_pending = false;
    s_cleanup_reply_generation = 0u;
    s_cleanup_mailbox_isrs = s_cleanup_mailbox_empty_status = 0u;
    // A restarted controller may reuse a generation; old breadcrumbs are not current.
    s_native_trace.magic = 0u;
    __DSB();
    SCB->DCCMVAC = (uint32_t)(uintptr_t)&s_native_trace;
    __DSB();
    alarm_info_t alarm = {
      .name = NATIVE_ALARM_NAME,
      .period_tick = ticks,
      .period_cnt = NATIVE_ALARM_ONESHOT,
      .callback = unhygienix_alarm_callback,
      .param_p = (void *)(uintptr_t)generation,
    };
    status = bk_pm_wakeup_source_set(PM_WAKEUP_SOURCE_INT_RTC, NULL);
    if (status == BK_OK) {
      s_native_armed = true;
      s_native_deadline = NATIVE_AON_COUNT_LOW + ticks;
      status = bk_alarm_register(AON_RTC_ID_1, &alarm);
      if (status != BK_OK)
        s_native_armed = false;
      if (status == BK_OK && sleep_request) {
        s_native_wake_only = operation == T5AIAlarm_ArmSleepWakeOnly;
        s_native_resources_only = operation == T5AIAlarm_ArmSleepResourcesOnly;
        s_native_retain_modules = operation == T5AIAlarm_ArmSleepRetainModules;
        s_native_mask_only = operation == T5AIAlarm_ArmSleepMaskOnly;
        s_native_mask_barrier = operation == T5AIAlarm_ArmSleepMaskBarrier || s_native_mask_only;
        s_native_shallow_wfi =
            operation == T5AIAlarm_ArmSleepAnalogShallowWFI || s_native_mask_barrier;
        s_native_domain_bypass =
            operation == T5AIAlarm_ArmSleepDomainBypass || s_native_retain_modules ||
            operation == T5AIAlarm_ArmSleepAnalogRetainDomains || s_native_shallow_wfi;
        s_native_bsp_restore = operation == T5AIAlarm_ArmSleepBspRestore;
        s_native_flash_restore = operation == T5AIAlarm_ArmSleepFlashRestore;
        s_native_core_only = operation == T5AIAlarm_ArmSleepCoreOnly || s_native_flash_restore ||
                             s_native_bsp_restore;
        s_native_analog_wake = analog_request;
        s_analog_prepare_lead = s_analog_prepare_reject = 0u;
        s_native_persistent_trace =
            operation == T5AIAlarm_ArmSleepPersistentTrace || s_native_analog_wake;
        s_native_vendor_lpo_halt =
            operation == T5AIAlarm_ArmSleepVendorLPOHalt || s_native_persistent_trace;
        s_native_vendor_masked = operation == T5AIAlarm_ArmSleepVendorMasked ||
                                 operation == T5AIAlarm_ArmSleepVendorMaskedKeepHF ||
                                 operation == T5AIAlarm_ArmSleepMaskedCPWFIKeepHF;
        s_native_vendor_subcores_on = operation == T5AIAlarm_ArmSleepVendorSubcoresOn ||
                                      operation == T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF ||
                                      s_native_vendor_masked;
        s_native_vendor_cpu_idle =
            operation == T5AIAlarm_ArmSleepVendorCPUIdle || s_native_vendor_subcores_on;
        s_native_vendor_flash_idle =
            operation == T5AIAlarm_ArmSleepVendorFlashIdle || s_native_vendor_cpu_idle;
        // Admit PMU sleep only after CPU0 reaches WFI, while AP remains published.
        if (operation == T5AIAlarm_ArmSleepMaskedCPWFIKeepHF) {
          s_native_vendor_cpu_idle = false;
          s_native_vendor_flash_idle = true;
        }
        s_native_no_hw_mask =
            !s_native_vendor_masked &&
            (operation == T5AIAlarm_ArmSleepNoHardwareMask ||
             operation == T5AIAlarm_ArmSleepNoHardwareMaskClocksOff || s_native_vendor_flash_idle);
        const bool real_wfi_keep_hf = operation == T5AIAlarm_ArmSleepKeepHF ||
                                      operation == T5AIAlarm_ArmSleepNoHardwareMask ||
                                      operation == T5AIAlarm_ArmSleepVendorSubcoresOnKeepHF ||
                                      operation == T5AIAlarm_ArmSleepVendorMaskedKeepHF ||
                                      operation == T5AIAlarm_ArmSleepMaskedCPWFIKeepHF;
        s_native_keep_hf = operation == T5AIAlarm_ArmSleepClocksLive || s_native_core_only ||
                           (s_native_domain_bypass && !analog_request) || real_wfi_keep_hf;
        s_native_clocks_only = operation == T5AIAlarm_ArmSleepClocksOnly ||
                               (s_native_keep_hf && !real_wfi_keep_hf && !s_native_mask_only);
        status = bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP, NATIVE_APP_AWAKE, 0u);
        s_native_app_released = false;
        if (status == BK_OK) {
          unhygienix_native_sleep_prepared = true;
          s_native_permit = true;
          aon_pmu_ll_set_r3_cp0_sleep_vote_state(1u);
          s_native_ready_flag = true;
        } else
          s_native_faulted = true;
      }
    }
    rtos_enable_int(irq_state);
  } else if (operation == T5AIAlarm_Cancel && generation != 0u &&
             generation == s_native_generation) {
    s_cleanup_cancel_progress = T5AICancelProgress_GenerationMatched;
    const uint32_t irq_state = rtos_disable_int();
    s_cleanup_cancel_progress = T5AICancelProgress_Locked;
    if (s_resources_active) {
      s_cleanup_cancel_progress = T5AICancelProgress_ResourcesActive;
      s_native_permit = false;
      prv_native_vote(false);
      s_native_faulted = true;
      rtos_enable_int(irq_state);
      if (bk_pm_cp0_response_cp1(T5AI_PM_NATIVE_ALARM_COMMAND, BK_FAIL, generation,
                                 T5AI_PM_NATIVE_ALARM_ABI) != BK_OK)
        s_native_faulted = true;
      return;
    }
    s_native_permit = false;
    prv_native_vote(false);
    s_cleanup_cancel_progress = T5AICancelProgress_VetoRestored;
    prv_native_revoke_ready();
    s_cleanup_cancel_progress = T5AICancelProgress_ReadyRevoked;
    if (s_lpo_saved) {
      aon_pmu_ll_set_r41((aon_pmu_ll_get_r41() & ~T5AI_SLEEP_LPO_HALT_MASK) | s_lpo_halt);
      s_lpo_saved = false;
    }
    if (s_analog_owned)
      s_native_faulted = true;
    s_native_resume_pending = false;
    unhygienix_native_sleep_prepared = false;
    s_native_cancelled = true;
    s_native_armed = false;
    s_native_fired = 0u;
    s_cleanup_cancel_progress = T5AICancelProgress_Disarmed;
    rtos_enable_int(irq_state);
    s_cleanup_cancel_progress = T5AICancelProgress_Unlocked;
    // RTC owns its list lock. A callback racing this call sees armed=false.
    s_cleanup_cancel_progress = T5AICancelProgress_Unregister;
    status = bk_alarm_unregister(AON_RTC_ID_1, (uint8_t *)NATIVE_ALARM_NAME);
    s_cleanup_cancel_progress = T5AICancelProgress_UnregisterReturned;
    const uint32_t cleanup_irq_state = rtos_disable_int();
    if (status != BK_OK)
      s_native_faulted = true;
    else if (!prv_persistent_restore())
      s_native_faulted = true;
    if (s_native_faulted)
      status = BK_FAIL;
    s_cleanup_cancel_progress = T5AICancelProgress_PersistentChecked;
    rtos_enable_int(cleanup_irq_state);
  }
  // Applied response: no foreign pointer or caller-owned payload survives ACK.
  if (operation == T5AIAlarm_Cancel) {
    // Cancel disarms the alarm; FinishCleanup closes the post-wake service lease.
    s_cleanup_cancel_progress = T5AICancelProgress_Reply;
  }
  const bk_err_t response_status = bk_pm_cp0_response_cp1(
      T5AI_PM_NATIVE_ALARM_COMMAND, (uint32_t)status, reply_generation, T5AI_PM_NATIVE_ALARM_ABI);
  if (operation == T5AIAlarm_Cancel)
    s_cleanup_cancel_progress = response_status == BK_OK ? T5AICancelProgress_ReplySubmitted
                                                         : T5AICancelProgress_ReplyFailed;
  if (response_status != BK_OK)
    s_native_faulted = true;
}
