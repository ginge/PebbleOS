/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>

#include "board/board.h"

#include "console/dbgserial.h"
#include "console/dbgserial_input.h"
#include "console/pulse.h"

#include <pbl/drivers/rtc.h>
#include <pbl/drivers/flash.h>
#include <pbl/drivers/debounced_button.h>

#include <pbl/drivers/accel.h>
#include <pbl/drivers/ambient_light.h>
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/battery.h>
#include <pbl/drivers/display/display.h>
#include <pbl/drivers/hrm.h>
#include <pbl/drivers/mag.h>
#include <pbl/drivers/mic.h>
#include <pbl/drivers/otp.h>
#include <pbl/drivers/pmic.h>
#include <pbl/drivers/pressure.h>
#include <pbl/task_wdt/task_wdt.h>
#include <pbl/drivers/temperature.h>
#include <pbl/drivers/touch/touch_sensor.h>
#include <pbl/drivers/vibe.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/drivers/sf32lb52/rc10k.h>
#ifdef CONFIG_SOC_T5AI
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/power.h>
#include <cmsis_core.h>

#endif

#include "resource/resource.h"
#include "resource/system_resource.h"

#include "kernel/util/task_init.h"
#include "kernel/events.h"
#include "kernel/kernel_heap.h"
#include "kernel/fault_handling.h"
#include "kernel/memory_layout.h"
#include "logging/pulse_logging.h"
#include "pbl/services/services.h"
#include "pbl/services/boot_splash.h"
#include "pbl/services/clock.h"
#include "pbl/cron/cron.h"
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
#include "kernel/util/delay.h"
#include "system/firmware_storage.h"
#include "system/passert.h"
#include "system/version.h"

#include "kernel/event_loop.h"

#include "console/serial_console.h"
#include "system/bootbits.h"
#include <pbl/logging/logging.h>

#include "debug/debug.h"

#include "pbl/kernel/irq.h"
#include "pbl/kernel/sched.h"
#include "pbl/kernel/thread.h"

#include "mfg/mfg_info.h"
#include "mfg/mfg_serials.h"

#include <pbl/bluetooth/init.h>
#ifdef CONFIG_QEMU
#include <pbl/drivers/qemu/qemu_serial.h>
#endif

#ifndef CONFIG_SOC_T5AI
static TimerID s_lowpower_timer = TIMER_INVALID_ID;
#ifndef CONFIG_MFG
static TimerID s_uptime_timer = TIMER_INVALID_ID;
#endif
#endif
static void main_task(void *parameter);

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
  pbl_irq_init();

  enable_fault_handlers();

  kernel_heap_init();

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
  struct pbl_thread_attr attr = {
    .name = "KernelMain",
    .entry = main_task,
    .prio = PBL_PRIO_IDLE + 3,
    .privileged = true,
    .stack = (void *)((uintptr_t)__kernel_main_stack_start__ + (uintptr_t)__stack_guard_size__),
    .stack_size = (uintptr_t)__kernel_main_stack_size__ - (uintptr_t)__stack_guard_size__,
  };

  pebble_task_create(PebbleTask_KernelMain, &attr);

  pbl_kernel_start();
}

#ifdef CONFIG_SOC_T5AI
#include <pbl/bluetooth/init.h>
#include <pbl/services/bluetooth/bluetooth_ctl.h>
#include <pbl/services/bluetooth/bluetooth_persistent_storage.h>

static void prv_t5ai_bluetooth_init(void) {
  /* The CP shell is ready before its flash and HCI services. Normal service
   * startup reaches this point well after reset on other platforms; preserve
   * that ordering here until CP service readiness is exposed through the
   * mailbox ABI. */
  pbl_thread_sleep(PBL_MSEC(2000));

  firmware_update_init();
  put_bytes_init();
  poll_remote_init();
  shared_prf_storage_init();
  bt_persistent_storage_init();
  comm_default_kernel_sender_init();
  comm_session_app_session_capabilities_init();
  comm_session_init();
  pbl_bt_init();
  bt_ctl_init();
}

#endif

static void init_drivers(void) {
  board_init();

  // The dbgserial input support requires timer support, so it is initialized here, much later
  // than the core dbgserial_init().
  dbgserial_input_init();

  serial_console_init();

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
}

static void clear_reset_loop_detection_bits(void) {
  boot_bit_clear(BOOT_BIT_RESET_LOOP_DETECT_ONE);
  boot_bit_clear(BOOT_BIT_RESET_LOOP_DETECT_TWO);
  boot_bit_clear(BOOT_BIT_RESET_LOOP_DETECT_THREE);
}

#if !defined(CONFIG_MFG) && !defined(CONFIG_SOC_T5AI)
static void uptime_callback(void *data) {
  PBL_LOG_VERBOSE("Uptime reached 15 minutes, set stable bit.");
  new_timer_delete(s_uptime_timer);
  boot_bit_set(BOOT_BIT_FW_STABLE);
}
#endif

#ifndef CONFIG_SOC_T5AI
static void prv_low_power_debug_config_callback(void *data) {
  new_timer_delete(s_lowpower_timer);
}
#endif

static PBL_NOINLINE void prv_main_task_init(void) {
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

#ifdef CONFIG_SOC_T5AI
  /* CP0 starts supervising CPU1 as soon as the pre-scheduler power-up
   * indication is acknowledged. Start mailbox dispatch and its dedicated
   * heartbeat task before storage/resource initialization can block
   * KernelMain longer than CP0's watchdog window. */
  t5ai_cp_mailbox_enable_rx_interrupt();
#endif

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

  // Suspend the task watchdog while the rest of the system comes up.
  pbl_task_wdt_init();
  pbl_task_wdt_suspend(30 * 1000);

  pbl_analytics_init();
  system_task_timer_init();

  init_drivers();

#ifdef CONFIG_SOC_T5AI
  /* Pebble does not yet participate in the vendor low-voltage suspend/resume
   * contract. Keep CP0 out of low voltage until that contract is implemented;
   * otherwise its Bluetooth controller can stop servicing a live connection
   * after the configured idle timeout. */
  const status_t app_sleep_vote_status = t5ai_cp_mailbox_vote_app_sleep(false);
  if (FAILED(app_sleep_vote_status)) {
    PBL_LOG_ERR("T5AI app sleep vote failed: %ld", (long)app_sleep_vote_status);
  }

  // T5AI elects its active clock once; the scheduler uses WFI for idle.

  boot_splash_start();
  backlight_set_brightness(BOARD_CONFIG.backlight_on_percent);
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
  pbl_cron_init();
  light_init();
  accel_manager_init();
#ifdef CONFIG_TOUCH
  touch_init();
#endif
#endif
  // Can't use the compositor framebuffer until the compositor is initialized
  compositor_init();
  kernel_ui_init();

#ifdef CONFIG_QEMU
  qemu_serial_init();
#endif

#ifdef CONFIG_SOC_T5AI
  t5ai_power_init();
#endif

#ifndef CONFIG_SOC_T5AI
  pbl_bt_init();
#else
  prv_t5ai_bluetooth_init();
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

  PBL_ASSERTN(pbl_task_wdt_add(NULL, CONFIG_TASK_WDT_TIMEOUT_MS, NULL, NULL) >= 0);

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

  pbl_task_wdt_resume();
}

static void main_task(void *parameter) {
#ifdef CONFIG_SOC_T5AI
  /* Ensure device interrupts are active as the first privileged task starts. */
  __enable_irq();
#endif
  prv_main_task_init();
  launcher_main_loop();
}
