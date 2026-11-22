/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <process_management/pebble_process_md.h>
#include FACTORY_SOURCE

static char calls[64];
static size_t used;
static unsigned worker_checks;
static const PebbleProcessMd worker;
static jmp_buf rebooted;
static void record(char step) {
  assert(used + 1 < sizeof(calls));
  calls[used++] = step;
}
void pbl_log_sync(uint8_t level, const char *file, int line, const char *fmt, ...) {
  record('L');
}
void pbl_sys_work_block(bool block) {
  assert(block);
  record('B');
}
void launcher_block_popups(bool block) {
  assert(block);
  record('P');
}
void worker_manager_disable(void) {
  record('W');
}
void event_service_clear_process_subscriptions(PebbleTask task) {
  assert(task == PebbleTask_App);
  record('C');
}
void shared_prf_storage_wipe_all(void) {
  record('S');
}
void services_set_runlevel(RunLevel level) {
  assert(level == RunLevel_BareMinimum);
  record('R');
}
void app_idle_timeout_stop(void) {
  record('T');
}
const PebbleProcessMd *worker_manager_get_current_worker_md(void) {
  return worker_checks++ < 2 ? &worker : NULL;
}
void pbl_sleep_ms(uint32_t ms) {
  assert(ms == 3);
  record('D');
}
void clock_timezone_clear(void) {
  record('Z');
}
void boot_bit_set(BootBitValue bit) {
  assert(bit == BOOT_BIT_FORCE_PRF);
  record('F');
}
#ifdef CONFIG_PBLBOOT
void firmware_storage_invalidate_firmware_slot(uint8_t slot) {
  assert(slot < 2);
  record('0' + slot);
}
#endif
int pbl_fs_format(const char *mount) {
  assert(!strcmp(mount, "/"));
  record('X');
  return 0;
}
#ifdef CONFIG_RECOVERY_FW
static const struct pbl_flash flash;
const struct pbl_flash *const FLASH = &flash;
int pbl_flash_erase(const struct pbl_flash *dev, uint32_t address, size_t length) {
  assert(dev == FLASH && address == PART_FILESYSTEM_OFFSET && length == PART_FILESYSTEM_SIZE);
  record('E');
  return 0;
}
#endif
int enter_standby(RebootReasonCode reason) {
  assert(reason == RebootReasonCode_FactoryResetShutdown);
  record('H');
  return -EBUSY;
}
void reboot_reason_set(RebootReason *reason) {
  assert(reason->code == RebootReasonCode_FactoryResetReset && reason->extra.value == 0);
  record('Q');
}
[[noreturn]] void pbl_reboot(void) {
  record('N');
  longjmp(rebooted, 1);
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  abort();
}
