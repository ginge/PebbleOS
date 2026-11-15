/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include <pbl/logging/backend_flash.h>
void pbl_log__flash_storage_init(void) {
}
uint32_t pbl_log__flash_storage_log_start(uint8_t length) {
  return UINT32_MAX;
}
bool pbl_log__flash_storage_write(const uint8_t *data, uint32_t addr, uint32_t length) {
  return false;
}
bool pbl_log_flash_dump(int generation, pbl_log_flash_dump_line_cb_t line,
                        pbl_log_flash_dump_done_cb_t done) {
  done(false);
  return false;
}
