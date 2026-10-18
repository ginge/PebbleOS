/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/otp.h>

#include <stddef.h>

char *otp_get_slot(const uint8_t index) {
  (void)index;
  return NULL;
}

uint8_t *otp_get_lock(const uint8_t index) {
  (void)index;
  return NULL;
}

bool otp_is_locked(const uint8_t index) {
  (void)index;
  return false;
}

OtpWriteResult otp_write_slot(const uint8_t index, const char *value) {
  (void)index;
  (void)value;
  return OtpWriteFailCorrupt;
}
