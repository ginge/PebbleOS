/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "debug/flash_logging.h"

void flash_logging_init(void) {}

uint32_t flash_logging_log_start(uint8_t msg_length) {
  (void)msg_length;
  return FLASH_LOG_INVALID_ADDR;
}

bool flash_logging_write(const uint8_t *data_to_write, uint32_t flash_addr,
                         uint32_t data_length) {
  (void)data_to_write;
  (void)flash_addr;
  (void)data_length;
  return false;
}

void flash_logging_set_enabled(bool enabled) {
  (void)enabled;
}

bool flash_dump_log_file(int generation, DumpLineCallback line_cb,
                         DumpCompletedCallback completed_cb) {
  (void)generation;
  (void)line_cb;
  completed_cb(false);
  return false;
}
