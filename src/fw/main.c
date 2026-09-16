/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <setjmp.h>

#include "debug/power_tracking.h"

#include "board/board.h"

#include "console/dbgserial.h"
#include "console/dbgserial_input.h"
#include "console/pulse.h"

#include <pbl/drivers/clocksource.h>
#include <pbl/drivers/rtc.h>
#include <pbl/drivers/flash.h>
#include <pbl/drivers/debounced_button.h>

#include <pbl/drivers/accel.h>
#include <pbl/drivers/ambient_light.h>
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/battery.h>
#include <pbl/drivers/display/display.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/hrm.h>
#include <pbl/drivers/mag.h>
#include <pbl/drivers/mic.h>
#include <pbl/drivers/otp.h>
#include <pbl/drivers/pmic.h>
#include <pbl/drivers/pressure.h>
#include <pbl/drivers/task_watchdog.h>
#include <pbl/drivers/temperature.h>
#include <pbl/drivers/touch/touch_sensor.h>
#include <pbl/drivers/vibe.h>
#include <pbl/drivers/voltage_monitor.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/drivers/sf32lb52/rc10k.h>
#ifdef CONFIG_SOC_T5AI
#include <pbl/drivers/display/t5ai.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/clock.h>
#include <pbl/soc/t5ai/freertos.h>
#include <pbl/soc/t5ai/rtc.h>
#endif

#include "resource/resource.h"
#include "resource/system_resource.h"

#include "kernel/util/task_init.h"
#include "kernel/util/sleep.h"
#include "kernel/events.h"
#include "kernel/kernel_heap.h"
#include "kernel/fault_handling.h"
#include "kernel/memory_layout.h"
#include "kernel/panic.h"
#include "kernel/pebble_tasks.h"
#include "kernel/remote_input.h"
#include "logging/pulse_logging.h"
#include "pbl/services/services.h"
#include "pbl/services/boot_splash.h"
#include "pbl/services/clock.h"
#include "pbl/services/cron.h"
#include "pbl/services/data_logging/data_logging_service.h"
#include "pbl/services/compositor/compositor.h"
#include "pbl/services/comm_session/app_session_capabilities.h"
#include "pbl/services/comm_session/default_kernel_sender.h"
#include "pbl/services/comm_session/session.h"
#include "pbl/services/firmware_update.h"
#include "pbl/services/poll_remote.h"
#include "pbl/services/put_bytes/put_bytes.h"
#include "pbl/services/services_normal.h"
#include "pbl/services/shared_prf_storage/shared_prf_storage.h"
#include "pbl/services/light.h"
#include "pbl/services/regular_timer.h"
#include "pbl/services/system_task.h"
#ifdef CONFIG_TOUCH
#include "pbl/services/touch/touch.h"
#endif
#include "pbl/services/new_timer/new_timer_service.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/accel_manager.h"
#include "pbl/services/prf_update.h"
#include "kernel/ui/kernel_ui.h"
#include "kernel/kernel_applib_state.h"
#include "process_management/app_manager.h"
#include "process_state/app_state/app_state.h"
#include "applib/ui/recognizer/touch_nav.h"
#include "kernel/util/delay.h"
#include "util/mbuf.h"
#include "system/firmware_storage.h"
#include "system/version.h"

#include "kernel/event_loop.h"

#include "applib/fonts/fonts.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text.h"
#include "applib/ui/ui.h"
#include "applib/ui/window_stack_private.h"

#include "console/serial_console.h"
#include "system/bootbits.h"
#include <pbl/logging/logging.h>
#include "system/passert.h"
#include "system/reset.h"

#include "syscall/syscall_internal.h"

#include "debug/debug.h"

#include "FreeRTOS.h"
#include "task.h"

#include "mfg/mfg_info.h"
#include "mfg/mfg_serials.h"

#include <bluetooth/init.h>

#include <string.h>

void soc_early_init(void);

/* here is as good as anywhere else ... */
const int __attribute__((used)) uxTopUsedPriority = configMAX_PRIORITIES - 1;

#ifndef CONFIG_SOC_T5AI
static TimerID s_lowpower_timer = TIMER_INVALID_ID;
#if !defined(CONFIG_MFG)
static TimerID s_uptime_timer = TIMER_INVALID_ID;
#endif
#endif
static void main_task(void* parameter);

static void print_splash_screen(void) {
#if defined(CONFIG_MFG)
  PBL_LOG_ALWAYS("PebbleOS - MANUFACTURING MODE");
#elif defined(CONFIG_RECOVERY_FW)
  PBL_LOG_ALWAYS("PebbleOS - RECOVERY MODE");
#else
  PBL_LOG_ALWAYS("PebbleOS");
#endif
  PBL_LOG_ALWAYS("%s%s", TINTIN_METADATA.version_tag,
                 (TINTIN_METADATA.is_dual_slot && !TINTIN_METADATA.is_recovery_firmware)
                     ? (TINTIN_METADATA.is_slot_0 ? " (slot0)" : " (slot1)")
                     : "");
  PBL_LOG_ALWAYS("(c) 2013-2026 The PebbleOS contributors");
  PBL_LOG_ALWAYS(" ");
}

int main(void) {
  soc_early_init();

  extern void* __ISR_VECTOR_TABLE__;  // Defined in linker script
  SCB->VTOR = (uint32_t)&__ISR_VECTOR_TABLE__;

  NVIC_SetPriorityGrouping(3);  // 4 bits for group priority; 0 bits for subpriority

  enable_fault_handlers();

  kernel_heap_init();

  mbuf_init();
  delay_init();
  dbgserial_init();
  pulse_early_init();
  print_splash_screen();

  rtc_init();

#ifdef CONFIG_RECOVERY_FW
  boot_bit_clear(BOOT_BIT_RECOVERY_START_IN_PROGRESS);
#endif

  extern uint32_t __kernel_main_stack_start__[];
  extern uint32_t __kernel_main_stack_size__[];
  extern uint32_t __stack_guard_size__[];
  const uint32_t kernel_main_stack_words =
      ((uint32_t)__kernel_main_stack_size__ - (uint32_t)__stack_guard_size__) /
      sizeof(portSTACK_TYPE);

  TaskParameters_t task_params = {
      .pvTaskCode = main_task,
      .pcName = "KernelMain",
      .usStackDepth = kernel_main_stack_words,
      .uxPriority = (tskIDLE_PRIORITY + 3) | portPRIVILEGE_BIT,
      .puxStackBuffer = (void*)(uintptr_t)((uint32_t)__kernel_main_stack_start__ +
                                           (uint32_t)__stack_guard_size__)};

  pebble_task_create(PebbleTask_KernelMain, &task_params, NULL);

  vTaskStartScheduler();
  for (;;);
}

static void watchdog_timer_callback(void* data) {
  task_watchdog_bit_set(PebbleTask_NewTimers);
}

#ifdef CONFIG_SOC_T5AI
#include <bluetooth/init.h>
#include <pbl/services/bluetooth/bluetooth_ctl.h>
#include <pbl/services/bluetooth/bluetooth_persistent_storage.h>

static char s_t5ai_timebase_response[480];
static T5AICPMailboxDiagnostics s_t5ai_mailbox_diagnostics;
static T5AICPMailboxFailureDiagnostics s_t5ai_mailbox_failure_diagnostics;
#define T5AI_SYSTEM_CLK_MODE1 (*(volatile uint32_t *)0x44010020u)
static T5AIClockDiagnostics s_t5ai_clock_diagnostics;
volatile uint32_t g_t5ai_boot_stage;
volatile uint32_t g_t5ai_app_stage;

static void prv_t5ai_print_retained_mailbox_failure(void) {
  if (!t5ai_cp_mailbox_get_failure_diagnostics(&s_t5ai_mailbox_failure_diagnostics)) {
    return;
  }
  const T5AICPMailboxFailureDiagnostics *failure = &s_t5ai_mailbox_failure_diagnostics;
  const uint32_t last = failure->trace_count == 0u ? 0u :
      MIN(failure->trace_count, T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY) - 1u;
  const T5AICPMailboxTraceEntry *entry = &failure->trace[last];
  dbgserial_putstr_fmt(
      s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
      "PBLIPCBOOT ctx=%08lx s=%ld tx=%lu/%lu/%lu bypass=%lu depth=%u high=%u "
      "rx=%lu ack=%lu route=%lu unroute=%lu senderr=%lu rxto=%lu ackmis=%lu "
      "head=%08lx/%08lx pending=%u rxpend=%u hw=%08lx/%08lx "
      "trace=%lu last=%lu/%u/%08lx/%08lx/%u/%ld",
      (unsigned long)failure->context, (long)failure->status,
      (unsigned long)failure->tx_queued, (unsigned long)failure->tx_published,
      (unsigned long)failure->tx_retired, (unsigned long)failure->tx_ack_bypass,
      (unsigned)failure->tx_depth, (unsigned)failure->tx_high_water,
      (unsigned long)failure->rx_frames, (unsigned long)failure->rx_transport_acks,
      (unsigned long)failure->rx_routed, (unsigned long)failure->rx_unrouted,
      (unsigned long)failure->send_failures, (unsigned long)failure->receive_timeouts,
      (unsigned long)failure->tx_ack_mismatches,
      (unsigned long)failure->tx_head_header, (unsigned long)failure->tx_head_parameter,
      (unsigned)failure->tx_pending, (unsigned)failure->rx_pending,
      (unsigned long)failure->mailbox_control, (unsigned long)failure->tx_fifo_status,
      (unsigned long)failure->trace_count,
      (unsigned long)entry->ticks, (unsigned)entry->event,
      (unsigned long)entry->header, (unsigned long)entry->parameter,
      (unsigned)entry->tx_depth, (long)entry->status);
  const uint32_t available =
      MIN(failure->trace_count, T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY);
  for (uint32_t i = 0u; i < available; ++i) {
    entry = &failure->trace[i];
    dbgserial_putstr_fmt(
        s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
        "PBLIPCBE n=%lu t=%lu e=%u h=%08lx p=%08lx d=%u s=%ld",
        (unsigned long)i, (unsigned long)entry->ticks, (unsigned)entry->event,
        (unsigned long)entry->header, (unsigned long)entry->parameter,
        (unsigned)entry->tx_depth, (long)entry->status);
  }
  t5ai_cp_mailbox_clear_failure_diagnostics();
}

static void prv_t5ai_bluetooth_init(void) {
  /* The CP shell is ready before its flash and HCI services. Normal service
   * startup reaches this point well after reset on other platforms; preserve
   * that ordering here until CP service readiness is exposed through the
   * mailbox ABI. */
  vTaskDelay(pdMS_TO_TICKS(2000));

  firmware_update_init();
  put_bytes_init();
  poll_remote_init();
  shared_prf_storage_init();
  bt_persistent_storage_init();
  comm_default_kernel_sender_init();
  comm_session_app_session_capabilities_init();
  comm_session_init();
  bt_driver_init();
  bt_ctl_init();
}

static void cp_heartbeat_timer_callback(void* data) {
  (void)t5ai_cp_mailbox_process_pending();
  t5ai_cp_mailbox_heartbeat();
  const DbgSerialMailboxProbe probe = dbgserial_input_consume_mailbox_probe();
  if (probe == DbgSerialMailboxProbe_Rx) {
    dbgserial_putstr("PBLRX-OK: T5AI USB mailbox RX passed");
  } else if (probe == DbgSerialMailboxProbe_Timebase) {
    TaskHandle_t app_task = pebble_task_get_handle_for_task(PebbleTask_App);
    TaskHandle_t kernel_task = pebble_task_get_handle_for_task(PebbleTask_KernelMain);
    TaskHandle_t background_task = pebble_task_get_handle_for_task(PebbleTask_KernelBackground);
    TaskHandle_t timer_task = pebble_task_get_handle_for_task(PebbleTask_NewTimers);
    TaskHandle_t bt_host_task = pebble_task_get_handle_for_task(PebbleTask_BTHost);
    TaskHandle_t bt_hci_task = pebble_task_get_handle_for_task(PebbleTask_BTHCI);
    EventQueueDiagnostics queue_diagnostics;
    event_get_queue_diagnostics(&queue_diagnostics);
    dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                         "PBLTIM freertos=%lu rtc=%lu aon=%lu core=%lu reload=%lu "
                         "stage=%lu appstage=%lu appinit=%u first=%u "
                         "apptask=%u state=%u prio=%u stack=%u km=%u/%u bg=%u/%u "
                         "nt=%u/%u/%u bth=%u/%u bti=%u/%u "
                         "q=%lu/%lu/%lu/%lu/%lu take=%lu cur=%lu "
                         "irq=%08lx/%08lx/%08lx",
                         (unsigned long)xTaskGetTickCount(), (unsigned long)rtc_get_ticks(),
                         (unsigned long)t5ai_rtc_get_raw_ticks(), (unsigned long)SystemCoreClock,
                         (unsigned long)SysTick->LOAD, (unsigned long)g_t5ai_boot_stage,
                         (unsigned long)g_t5ai_app_stage,
                         (unsigned)app_manager_is_initialized(),
                         (unsigned)app_manager_is_first_app_launched(), (unsigned)(app_task != NULL),
                         app_task ? (unsigned)eTaskGetState(app_task) : 0xffu,
                         app_task ? (unsigned)uxTaskPriorityGet(app_task) : 0xffu,
                         app_task ? (unsigned)uxTaskGetStackHighWaterMark(app_task) : 0u,
                         kernel_task ? (unsigned)eTaskGetState(kernel_task) : 0xffu,
                         kernel_task ? (unsigned)uxTaskGetStackHighWaterMark(kernel_task) : 0u,
                         background_task ? (unsigned)eTaskGetState(background_task) : 0xffu,
                         background_task ? (unsigned)uxTaskGetStackHighWaterMark(background_task) : 0u,
                         timer_task ? (unsigned)eTaskGetState(timer_task) : 0xffu,
                         timer_task ? (unsigned)uxTaskPriorityGet(timer_task) : 0xffu,
                         timer_task ? (unsigned)uxTaskGetStackHighWaterMark(timer_task) : 0u,
                         bt_host_task ? (unsigned)eTaskGetState(bt_host_task) : 0xffu,
                         bt_host_task ? (unsigned)uxTaskPriorityGet(bt_host_task) : 0xffu,
                         bt_hci_task ? (unsigned)eTaskGetState(bt_hci_task) : 0xffu,
                         bt_hci_task ? (unsigned)uxTaskPriorityGet(bt_hci_task) : 0xffu,
                         (unsigned long)queue_diagnostics.set_depth,
                         (unsigned long)queue_diagnostics.kernel_depth,
                         (unsigned long)queue_diagnostics.app_depth,
                         (unsigned long)queue_diagnostics.worker_depth,
                         (unsigned long)queue_diagnostics.self_depth,
                         (unsigned long)queue_diagnostics.events_taken,
                         (unsigned long)queue_diagnostics.current_event,
                         (unsigned long)SCB->ICSR, (unsigned long)__get_BASEPRI(),
                         (unsigned long)__get_PRIMASK());
  } else if (probe == DbgSerialMailboxProbe_Mailbox) {
    t5ai_cp_mailbox_get_diagnostics(&s_t5ai_mailbox_diagnostics);
    dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                         "PBLIPC tx=%lu/%lu/%lu ack=%lu depth=%u high=%u rx=%lu ack=%lu route=%lu "
                         "unroute=%lu senderr=%lu rxto=%lu ackmis=%lu trace=%lu",
                         (unsigned long)s_t5ai_mailbox_diagnostics.tx_queued,
                         (unsigned long)s_t5ai_mailbox_diagnostics.tx_published,
                         (unsigned long)s_t5ai_mailbox_diagnostics.tx_retired,
                         (unsigned long)s_t5ai_mailbox_diagnostics.tx_ack_bypass,
                         (unsigned)s_t5ai_mailbox_diagnostics.tx_depth,
                         (unsigned)s_t5ai_mailbox_diagnostics.tx_high_water,
                         (unsigned long)s_t5ai_mailbox_diagnostics.rx_frames,
                         (unsigned long)s_t5ai_mailbox_diagnostics.rx_transport_acks,
                         (unsigned long)s_t5ai_mailbox_diagnostics.rx_routed,
                         (unsigned long)s_t5ai_mailbox_diagnostics.rx_unrouted,
                         (unsigned long)s_t5ai_mailbox_diagnostics.send_failures,
                         (unsigned long)s_t5ai_mailbox_diagnostics.receive_timeouts,
                         (unsigned long)s_t5ai_mailbox_diagnostics.tx_ack_mismatches,
                         (unsigned long)s_t5ai_mailbox_diagnostics.trace_count);
    const uint32_t available =
        MIN(s_t5ai_mailbox_diagnostics.trace_count, T5AI_CP_MAILBOX_TRACE_CAPACITY);
    const uint32_t first = s_t5ai_mailbox_diagnostics.trace_count - available;
    for (uint32_t i = first; i < s_t5ai_mailbox_diagnostics.trace_count; ++i) {
      const T5AICPMailboxTraceEntry *entry =
          &s_t5ai_mailbox_diagnostics.trace[i % T5AI_CP_MAILBOX_TRACE_CAPACITY];
      dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                           "PBLIPCE n=%lu t=%lu e=%u h=%08lx p=%08lx d=%u s=%ld", (unsigned long)i,
                           (unsigned long)entry->ticks, (unsigned)entry->event,
                           (unsigned long)entry->header, (unsigned long)entry->parameter,
                           (unsigned)entry->tx_depth, (long)entry->status);
    }
    if (t5ai_cp_mailbox_get_failure_diagnostics(&s_t5ai_mailbox_failure_diagnostics)) {
      dbgserial_putstr_fmt(
          s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
          "PBLIPCF ctx=%08lx s=%ld t=%lu tx=%lu/%lu/%lu ack=%lu depth=%u high=%u "
          "rx=%lu ack=%lu route=%lu unroute=%lu senderr=%lu rxto=%lu ackmis=%lu trace=%lu",
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.context,
          (long)s_t5ai_mailbox_failure_diagnostics.status,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.ticks,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.tx_queued,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.tx_published,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.tx_retired,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.tx_ack_bypass,
          (unsigned)s_t5ai_mailbox_failure_diagnostics.tx_depth,
          (unsigned)s_t5ai_mailbox_failure_diagnostics.tx_high_water,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.rx_frames,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.rx_transport_acks,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.rx_routed,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.rx_unrouted,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.send_failures,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.receive_timeouts,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.tx_ack_mismatches,
          (unsigned long)s_t5ai_mailbox_failure_diagnostics.trace_count);
      for (uint32_t i = 0; i < T5AI_CP_MAILBOX_FAILURE_TRACE_CAPACITY; ++i) {
        const T5AICPMailboxTraceEntry *entry = &s_t5ai_mailbox_failure_diagnostics.trace[i];
        dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                             "PBLIPCFE n=%lu t=%lu e=%u h=%08lx p=%08lx d=%u s=%ld",
                             (unsigned long)i, (unsigned long)entry->ticks,
                             (unsigned)entry->event, (unsigned long)entry->header,
                             (unsigned long)entry->parameter, (unsigned)entry->tx_depth,
                             (long)entry->status);
      }
    }
  } else if (probe == DbgSerialMailboxProbe_Display) {
    display_co5300_log_diagnostics();
  } else if (probe == DbgSerialMailboxProbe_DisplayBitbang) {
    display_co5300_bitbang_probe();
  } else if (probe == DbgSerialMailboxProbe_Splash) {
    boot_splash_start();
    dbgserial_putstr("PBLSPL done");
  } else if (probe >= DbgSerialMailboxProbe_ButtonBack &&
             probe <= DbgSerialMailboxProbe_ButtonDown) {
    const ButtonId button = (ButtonId)(probe - DbgSerialMailboxProbe_ButtonBack);
    const RemoteInputResult result = remote_input_button_press(button, 1u, 120u, 0u);
    dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                         "PBLB%u result=%u", (unsigned)button, (unsigned)result);
  } else if (probe == DbgSerialMailboxProbe_Touch) {
    TouchSensorDiagnostics diagnostics;
    touch_sensor_get_diagnostics(&diagnostics);
    dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                         "PBLTP global=%u nav=%u app=%u enabled=%u down=%u "
                         "polls=%lu fail=%lu ack=%lu valid=%lu n=%u chg=%u "
                         "first=%d,%d min=%d,%d max=%d,%d last=%d,%d "
                         "raw=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                         (unsigned)touch_service_is_globally_enabled(),
                         (unsigned)touch_nav_enabled(), (unsigned)touch_app_nav_active(),
                         (unsigned)diagnostics.enabled, (unsigned)diagnostics.finger_down,
                         (unsigned long)diagnostics.polls,
                         (unsigned long)diagnostics.read_failures,
                         (unsigned long)diagnostics.ack_reports,
                         (unsigned long)diagnostics.valid_reports,
                         (unsigned)diagnostics.contact_samples,
                         (unsigned)diagnostics.coordinate_changes,
                         diagnostics.first_x, diagnostics.first_y,
                         diagnostics.min_x, diagnostics.min_y,
                         diagnostics.max_x, diagnostics.max_y,
                         diagnostics.last_x, diagnostics.last_y,
                         diagnostics.last_report[0], diagnostics.last_report[1],
                         diagnostics.last_report[2], diagnostics.last_report[3],
                         diagnostics.last_report[4], diagnostics.last_report[5],
                         diagnostics.last_report[6], diagnostics.last_report[7],
                         diagnostics.last_report[8], diagnostics.last_report[9]);
  } else if (probe == DbgSerialMailboxProbe_TouchNav) {
    const TouchNavState *state = app_state_get_touch_nav_state();
    const TouchServiceState *touch_state = app_state_get_touch_service_state();
    TouchServiceDiagnostics service;
    TouchEventLoopDiagnostics event_loop;
    touch_get_diagnostics(&service);
    event_loop_get_touch_diagnostics(&event_loop);
    dbgserial_putstr_fmt(
        s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
        "PBLTN ev=%u/%u/%u widget=%u/%u/%u/%u route=%u mgr=%u "
        "menu=%u target=%u bridge=%u/%u/%u/%u/%u/%u "
        "svc=%lu/%lu/%lu/%lu/%lu/%u/%u/%u loop=%lu/%lu/%lu/%lu sub=%u/%u",
        state->counters.touchdowns, state->counters.position_updates,
        state->counters.liftoffs, state->counters.widget_started,
        state->counters.widget_updated, state->counters.widget_completed,
        state->counters.widget_cancelled, (unsigned)state->route,
        (unsigned)state->manager->state, (unsigned)(state->menu_head != NULL),
        (unsigned)(state->latched_target != NULL), state->counters.started,
        state->counters.completed, state->counters.failed,
        state->counters.cancelled, state->counters.dropped, state->counters.gated,
        (unsigned long)service.physical_samples,
        (unsigned long)service.physical_samples_ignored,
        (unsigned long)service.queued_touchdowns,
        (unsigned long)service.queued_position_updates,
        (unsigned long)service.queued_liftoffs, (unsigned)service.touch_state,
        (unsigned)service.injecting, (unsigned)service.subscriber_count,
        (unsigned long)event_loop.events, (unsigned long)event_loop.unarmed_touchdowns,
        (unsigned long)event_loop.masked_animation, (unsigned long)event_loop.masked_modal,
        (unsigned)touch_state->subscribed, (unsigned)(touch_state->system_handler != NULL));
  } else if (probe == DbgSerialMailboxProbe_SwipeUp) {
    const RemoteInputResult result =
        remote_input_swipe(RemoteInputSwipeDirection_Up,
                           REMOTE_INPUT_SWIPE_DEFAULT_DURATION_MS);
    dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                         "PBLSU result=%u", (unsigned)result);
  }
}
#endif

static void register_system_timers(void) {
  static RegularTimerInfo watchdog_timer = {.list_node = {0, 0}, .cb = watchdog_timer_callback};
  regular_timer_add_seconds_callback(&watchdog_timer);
#ifdef CONFIG_SOC_T5AI
  static RegularTimerInfo cp_heartbeat_timer = {
      .list_node = {0, 0},
      .cb = cp_heartbeat_timer_callback,
  };
  regular_timer_add_seconds_callback(&cp_heartbeat_timer);
#endif
}

static void init_drivers(void) {
  board_init();

  // The dbgserial input support requires timer support, so it is initialized here, much later
  // than the core dbgserial_init().
  dbgserial_input_init();

  serial_console_init();

#ifdef HAS_DRIVER_VOLTAGE_MONITOR
  voltage_monitor_init();
#endif

  battery_init();
  vibe_init();

#ifdef CONFIG_PMIC
  pmic_init();
#endif

  flash_init();
  flash_sleep_when_idle(true);
  flash_enable_write_protection();
  flash_prf_set_protection(true);

#ifndef CONFIG_SOC_T5AI
  uint8_t vibe_cali = mfg_info_get_vibe_cali();
  if (vibe_cali != MFG_INFO_VIBE_CALI_INVALID) {
    vibe_apply_calibration(vibe_cali);
  }
#endif

#ifdef CONFIG_MIC
  mic_init(MIC);
#endif

#ifdef CONFIG_TOUCH
  touch_sensor_init();
#endif

  accel_init();

#ifdef CONFIG_MAG
  mag_init();
#endif
#ifdef CONFIG_PRESSURE
  pressure_init();
#endif

  backlight_init();
  ambient_light_init();

  temperature_init();

  rtc_init_timers();
  rtc_alarm_init();

  power_tracking_init();
}

static void clear_reset_loop_detection_bits(void) {
  boot_bit_clear(BOOT_BIT_RESET_LOOP_DETECT_ONE);
  boot_bit_clear(BOOT_BIT_RESET_LOOP_DETECT_TWO);
  boot_bit_clear(BOOT_BIT_RESET_LOOP_DETECT_THREE);
}

#if !defined(CONFIG_MFG) && !defined(CONFIG_SOC_T5AI)
static void uptime_callback(void* data) {
  PBL_LOG_VERBOSE("Uptime reached 15 minutes, set stable bit.");
  new_timer_delete(s_uptime_timer);
  boot_bit_set(BOOT_BIT_FW_STABLE);
}
#endif

#ifndef CONFIG_SOC_T5AI
static void prv_low_power_debug_config_callback(void* data) {
  new_timer_delete(s_lowpower_timer);
}
#endif

static NOINLINE void prv_main_task_init(void) {
  // The Snowy bootloader does not clear the watchdog flag itself. Clear the
  // flag ourselves so that a future safe reset does not look like a watchdog
  // reset to the bootloader.
  static McuRebootReason s_mcu_reboot_reason;
  s_mcu_reboot_reason = watchdog_clear_reset_flag();

#ifdef CONFIG_PULSE_EVERYWHERE
  pulse_init();
  pulse_logging_init();
#endif

  pebble_task_configure_idle_task();

  task_init();

  memory_layout_setup_mpu();

  board_early_init();

#ifndef CONFIG_SOC_T5AI
  boot_splash_start();
#endif

  kernel_applib_init();

  system_task_init();

  events_init();

  new_timer_service_init();
  regular_timer_init();

  // Initialize the task watchdog and immediately pause it for 30 seconds to
  // give us time to initialize everything without worrying about task watchdog
  // from firing if we block other tasks.
  task_watchdog_init();
  task_watchdog_pause(30);

  pbl_analytics_init();
  register_system_timers();
  system_task_timer_init();

  init_drivers();

#ifdef CONFIG_SOC_T5AI
  /* Diagnostic: retain the corrected MPU cacheability policy while returning
   * CPU1 to its lowest normal active frequency. This isolates the cache fix
   * from the earlier 480 MHz performance experiment. */
  const status_t cpu_clock_vote_status =
      t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency_120MHz);
  if (cpu_clock_vote_status == S_SUCCESS) {
    SystemCoreClock = 120000000u;
    t5ai_freertos_apply_systick_clock();
  }
  s_t5ai_clock_diagnostics = t5ai_clock_get_diagnostics();
  dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                       "PBLCLK vote=%ld mode1=%08lx cpu1=%08lx measured=%lu core=%lu "
                       "systick=%08lx/%lu/%lu",
                       (long)cpu_clock_vote_status, (unsigned long)s_t5ai_clock_diagnostics.mode1,
                       (unsigned long)s_t5ai_clock_diagnostics.cpu1_control,
                       (unsigned long)s_t5ai_clock_diagnostics.measured_hz,
                       (unsigned long)SystemCoreClock, (unsigned long)SysTick->CTRL,
                       (unsigned long)SysTick->LOAD, (unsigned long)SysTick->VAL);
  prv_t5ai_print_retained_mailbox_failure();

  /* Normal firmware's boot splash service is intentionally a no-op, so T5AI
   * must initialize its panel explicitly after the peripheral drivers. */
  display_init();
  boot_splash_start();
  backlight_set_brightness(BOARD_CONFIG.backlight_on_percent);
  g_t5ai_boot_stage = 1u;
#endif

  clock_init();

#if defined(CONFIG_IS_BIGBOARD)
  // Program a random S/N into the Bigboard in case it's not been done yet:
  mfg_write_bigboard_serial_number();
#endif

#if defined(CONFIG_MFG)
  mfg_info_update_constant_data();
#endif

  debug_init(s_mcu_reboot_reason);

#ifdef CONFIG_SOC_T5AI
  PBL_LOG_ERR("T5AI clock mode1=%08lx cpu1=%08lx measured=%lu configured=%lu",
              (unsigned long)s_t5ai_clock_diagnostics.mode1,
              (unsigned long)s_t5ai_clock_diagnostics.cpu1_control,
              (unsigned long)s_t5ai_clock_diagnostics.measured_hz,
              (unsigned long)SystemCoreClock);
#endif

  services_early_init();

#ifndef CONFIG_SOC_T5AI
  debug_print_last_launched_app();

  // Do this early before things can screw ith it.
  check_prf_update();
#endif

#if defined(CONFIG_PBLBOOT) && defined(CONFIG_RECOVERY_FW) && !defined(CONFIG_MFG)
  // Invalidate slot0/1 when booting PRF, so we force main firmware re-install
  firmware_storage_invalidate_firmware_slot(0);
  firmware_storage_invalidate_firmware_slot(1);
#endif

  // When there are new system resources waiting to be installed, this call
  // will actually install them:
  resource_init();

  system_resource_init();

#ifdef CONFIG_SOC_T5AI
  g_t5ai_boot_stage = 2u;
#endif

#ifdef CONFIG_HRM
  hrm_init(HRM);
#endif

  // The display has to be initialized before bluetooth because on Snowy the
  // display FPGA shares the 32 kHz clock signal with bluetooth. If the FPGA is
  // not programmed when we attempt to initialize bluetooth, it prevents the
  // clock from reaching the bluetooth module and initialization fails.
#ifndef CONFIG_SOC_T5AI
  display_init();
#endif

  // Stop boot splash before initializing compositor
  boot_splash_stop();
#ifdef CONFIG_SOC_T5AI
  /* Initialize the subset of common services supported by the T5AI mailbox
   * integration. Persistent services are initialized around Bluetooth below. */
  cron_service_init();
  light_init();
  accel_manager_init();
#ifdef CONFIG_TOUCH
  touch_init();
#endif
#endif
  // Can't use the compositor framebuffer until the compositor is initialized
  compositor_init();
  kernel_ui_init();

#ifdef CONFIG_SOC_T5AI
  g_t5ai_boot_stage = 3u;
  t5ai_freertos_apply_systick_clock();
  dbgserial_putstr_fmt(s_t5ai_timebase_response, sizeof(s_t5ai_timebase_response),
                       "PBLCLK apply mode1=%08lx core=%lu systick=%08lx/%lu/%lu",
                       (unsigned long)T5AI_SYSTEM_CLK_MODE1, (unsigned long)SystemCoreClock,
                       (unsigned long)SysTick->CTRL, (unsigned long)SysTick->LOAD,
                       (unsigned long)SysTick->VAL);

  /* CP0's shell fallback is interrupt-scale. Defer mailbox dispatch from IRQ
   * 63 to the high-priority NewTimer work queue after hardware bring-up. */
  t5ai_cp_mailbox_enable_rx_interrupt();
#endif

#ifndef CONFIG_SOC_T5AI
  bt_driver_init();
#else
  prv_t5ai_bluetooth_init();
  g_t5ai_boot_stage = 4u;
#endif

#ifndef CONFIG_SOC_T5AI
  services_init();
#else
  // T5AI initializes hardware-facing common services in its board-specific
  // sequence above. Its writable PFS is now available, so initialize the full
  // set of normal services required by the shared protocol endpoint table.
  services_normal_init();
  // Keep the controller initialized but unavailable to phones until persistent
  // services (especially Activity on a fallback RTC) have finished their
  // synchronous PFS startup. Otherwise an early SetTime can block KernelMain
  // while PPoG callbacks fill its event queue.
  bt_ctl_set_enabled(true);
#endif

  // The RTC needs be calibrated after the mfg registry service has been initialized so we can
  // load the measured frequency.
#if defined(CONFIG_SOC_SF32LB52) && !defined(SF32LB52_USE_LXT)
  rc10k_init();
#endif
#ifndef CONFIG_SOC_T5AI
  rtc_calibrate_frequency(mfg_info_get_rtc_freq());
#endif

  clear_reset_loop_detection_bits();

#ifndef CONFIG_SOC_T5AI
  task_watchdog_mask_set(PebbleTask_KernelMain);
#endif

#ifndef CONFIG_SOC_T5AI
  // Leave the board with stop and sleep mode debugging enabled for at least 10
  // seconds to give OpenOCD time to start and still able to connect when it is
  // ready to flash in the new image via JTAG
  s_lowpower_timer = new_timer_create();
  new_timer_start(s_lowpower_timer, 10 * 1000, prv_low_power_debug_config_callback, NULL,
                  0 /*flags*/);

#ifndef CONFIG_MFG
  s_uptime_timer = new_timer_create();
  new_timer_start(s_uptime_timer, 15 * 60 * 1000, uptime_callback, NULL, 0 /*flags*/);
#else
  boot_bit_set(BOOT_BIT_FW_STABLE);
#endif
#endif

  // Initialize button driver at the last moment to prevent "system on" button press from
  // entering the kernel event queue.
  debounced_button_init();

#ifdef CONFIG_SOC_T5AI
  g_t5ai_boot_stage = 5u;
#endif

#ifndef CONFIG_SOC_T5AI
  task_watchdog_resume();
#endif
}

static void main_task(void* parameter) {
#ifdef CONFIG_SOC_T5AI
  /* The pinned CM33 handoff reaches the first task with PRIMASK set. FreeRTOS
   * restores BASEPRI itself, but device and SysTick IRQs remain globally
   * suppressed until the first privileged kernel task clears PRIMASK. */
  __enable_irq();
#endif
  prv_main_task_init();
#ifdef CONFIG_SOC_T5AI
  g_t5ai_boot_stage = 6u;
#endif
  launcher_main_loop();
}
