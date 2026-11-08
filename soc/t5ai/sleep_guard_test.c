/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/section.h>
#include <pbl/logging/logging.h>
#include <pbl/mcu/cache.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/hci_ipc.h>
#include <pbl/soc/t5ai/mailbox_channel.h>
#include <pbl/soc/t5ai/mb_ipc.h>
#include <pbl/soc/t5ai/native_alarm.h>
#include <pbl/soc/t5ai/reset.h>
#include <pbl/soc/t5ai/sleep.h>

#include <cmsis_core.h>

#define GUARD_TEST_SLOT_SECONDS       2u
#define GUARD_TEST_WAIT_STEPS         180u
#define GUARD_ATTEMPT_MAGIC           0x47524436u
#define GUARD_TRACE_ALIGNMENT         32u
#define GUARD_AON_SNAPSHOT_ADDRESS    0x440001e8u
#define GUARD_AON_LIVE_REASON_ADDRESS 0x44000000u
#define GUARD_TEST_SETTLE_MS          30000u
#define GUARD_TEST_RETRY_MS           1000u
#define GUARD_TEST_ADMISSION_RETRY_MS 20u
#define GUARD_MS_PER_SECOND           1000u
#define GUARD_TEST_ATTEMPTS \
  (GUARD_TEST_SLOT_SECONDS * GUARD_MS_PER_SECOND / GUARD_TEST_ADMISSION_RETRY_MS)
#define GUARD_AON_LPO_ADDRESS     0x44000104u
#define GUARD_AON_CONTROL_ADDRESS 0x44000600u
#define GUARD_READ(address)       (*(volatile uint32_t *)(address))
PBL_LOG_MODULE_DEFINE(t5ai_sleep_guard, LOG_LEVEL_DEBUG);
static volatile struct {
  uint32_t magic, inverse;
} s_guard_attempt __attribute__((section(".t5ai_sleep_trace"), aligned(GUARD_TRACE_ALIGNMENT)));
static uint32_t s_guard_generation;
static TimerID s_guard_timer;
static bool s_guard_started, s_guard_reported;
static unsigned s_guard_attempts, s_guard_wait_steps;
static uint32_t s_guard_seen_owners, s_guard_last_owners;
static unsigned s_guard_last_ready;
static bool s_guard_faulted;
enum {
  GuardReady_Mailbox = 1u << 0,
  GuardReady_Ipc = 1u << 1,
  GuardReady_Hci = 1u << 2,
  GuardReady_All = GuardReady_Mailbox | GuardReady_Ipc | GuardReady_Hci,
};
static void prv_schedule(void *context);

[[noreturn]] PBL_SECTION_RAM static void prv_guard_wait(void) {
  __disable_irq();
  __DSB();
  while (true)
    __WFI();
}

static void prv_cancel_lease(void) {
  if (s_guard_generation && t5ai_native_alarm_cancel(s_guard_generation) != S_SUCCESS)
    PBL_LOG_ERR("T5GUARD lease cancel failed; recovery required");
  s_guard_generation = 0u;
}

static bool prv_slot_expired(time_t now, time_t slot) {
  return now >= slot && (uint32_t)(now - slot) >= GUARD_TEST_SLOT_SECONDS;
}

static void prv_report_admission(time_t now) {
  PBL_LOG_DBG("T5GUARD admission now=%ld attempts=%u owners=%lx seen=%lx ready=%x fault=%u",
              (long)now, s_guard_attempts, (unsigned long)s_guard_last_owners,
              (unsigned long)s_guard_seen_owners, s_guard_last_ready, s_guard_faulted);
}

static void prv_run(void *context) {
  (void)context;
  const time_t now = rtc_get_time();
  const time_t slot = CONFIG_T5AI_SLEEP_GUARD_TEST_AT_UTC;
  if (!slot || prv_slot_expired(now, slot) ||
      (now < slot && ++s_guard_wait_steps >= GUARD_TEST_WAIT_STEPS)) {
    prv_cancel_lease();
    new_timer_delete(s_guard_timer);
    s_guard_timer = TIMER_INVALID_ID;
    prv_report_admission(now);
    PBL_LOG_DBG("T5GUARD UTC slot closed/wait exhausted; no WFI");
    return;
  }
  if (!s_guard_reported) {
    status_t status = t5ai_native_alarm_init();
    if (status == S_SUCCESS)
      status = t5ai_native_alarm_prepare_sleep(T5AI_PM_NATIVE_ALARM_MAX_TICKS, &s_guard_generation);
    if (status != S_SUCCESS) {
      prv_cancel_lease();
      new_timer_delete(s_guard_timer);
      s_guard_timer = TIMER_INVALID_ID;
      PBL_LOG_ERR("T5GUARD CP lease rejected; no WFI");
      return;
    }
    // No AP-WFI publication: CP keeps APP awake and only leases its AON writes.
    PBL_LOG_DBG("T5GUARD probe lpo=%lx control=%lx",
                (unsigned long)GUARD_READ(GUARD_AON_LPO_ADDRESS),
                (unsigned long)GUARD_READ(GUARD_AON_CONTROL_ADDRESS));
    s_guard_reported = true;
    (void)new_timer_start(s_guard_timer, GUARD_TEST_RETRY_MS, prv_schedule, NULL, 0);
    return;
  }
  if (now < slot) {
    (void)new_timer_start(s_guard_timer, GUARD_TEST_RETRY_MS, prv_schedule, NULL, 0);
    return;
  }
  pbl_irq_lock();
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const T5AISleepBlockers work = t5ai_sleep_blockers();
  unsigned ready = 0u;
  if (t5ai_mailbox_channel_sleep_ready())
    ready |= GuardReady_Mailbox;
  if (t5ai_mb_ipc_sleep_ready())
    ready |= GuardReady_Ipc;
  if (t5ai_hci_sleep_ready())
    ready |= GuardReady_Hci;
  s_guard_last_owners = work.owners;
  s_guard_seen_owners |= work.owners;
  s_guard_last_ready = ready;
  s_guard_faulted = work.faulted;
  const bool quiet = !work.owners && !work.faulted && ready == GuardReady_All;
  if (!quiet) {
    __set_PRIMASK(primask);
    pbl_irq_unlock();
    if (++s_guard_attempts < GUARD_TEST_ATTEMPTS && !work.faulted) {
      (void)new_timer_start(s_guard_timer, GUARD_TEST_ADMISSION_RETRY_MS, prv_schedule, NULL, 0);
      return;
    }
    prv_cancel_lease();
    new_timer_delete(s_guard_timer);
    s_guard_timer = TIMER_INVALID_ID;
    prv_report_admission(now);
    PBL_LOG_ERR("T5GUARD busy/fault budget exhausted; no WFI");
    return;
  }
  // Best-effort evidence only; the UTC slot bounds retries after whole-chip reset.
  s_guard_attempt.magic = GUARD_ATTEMPT_MAGIC;
  s_guard_attempt.inverse = ~GUARD_ATTEMPT_MAGIC;
  dcache_flush((const void *)&s_guard_attempt, sizeof(s_guard_attempt));
  __DSB();
  // Cache maintenance must not carry admission beyond the expiring slot.
  const time_t arm_time = rtc_get_time();
  if (arm_time < slot || prv_slot_expired(arm_time, slot)) {
    __set_PRIMASK(primask);
    pbl_irq_unlock();
    prv_cancel_lease();
    new_timer_delete(s_guard_timer);
    s_guard_timer = TIMER_INVALID_ID;
    PBL_LOG_DBG("T5GUARD slot closed at arm boundary; no WFI");
    return;
  }
  if (!t5ai_watchdog_sleep_guard_arm()) {
    __set_PRIMASK(primask);
    pbl_irq_unlock();
    prv_cancel_lease();
    new_timer_delete(s_guard_timer);
    s_guard_timer = TIMER_INVALID_ID;
    PBL_LOG_ERR("T5GUARD rejected before WFI");
    return;
  }
  prv_guard_wait();
}

static void prv_schedule(void *context) {
  (void)context;
  if (!system_task_add_callback_droppable(prv_run, NULL)) {
    prv_cancel_lease();
    new_timer_delete(s_guard_timer);
    s_guard_timer = TIMER_INVALID_ID;
    PBL_LOG_ERR("T5GUARD scheduling failed; no WFI");
  }
}

void t5ai_sleep_guard_test_start(void) {
  if (s_guard_started)
    return;
  s_guard_started = true;
  const time_t slot = CONFIG_T5AI_SLEEP_GUARD_TEST_AT_UTC;
  if (!slot || prv_slot_expired(rtc_get_time(), slot)) {
    PBL_LOG_DBG("T5GUARD no future UTC slot; test disabled");
    return;
  }
  const uint8_t boot = t5ai_reset_reason_get_boot_ap();
  const bool attempted = s_guard_attempt.magic == GUARD_ATTEMPT_MAGIC &&
                         s_guard_attempt.inverse == ~GUARD_ATTEMPT_MAGIC;
  PBL_LOG_DBG("T5GUARD boot reason=%x attempted=%u r0=%lx snapshot=%lx", boot, attempted,
              (unsigned long)GUARD_READ(GUARD_AON_LIVE_REASON_ADDRESS),
              (unsigned long)GUARD_READ(GUARD_AON_SNAPSHOT_ADDRESS));
  if (boot == T5AI_RESET_REASON_SLEEP_RESUME || attempted)
    return;
  s_guard_timer = new_timer_create();
  (void)new_timer_start(s_guard_timer, GUARD_TEST_SETTLE_MS, prv_schedule, NULL, 0);
}
