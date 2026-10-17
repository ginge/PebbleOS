/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/time.h>
#include "system/bootbits.h"

#include <pbl/logging/logging.h>
#include <pbl/kernel/sched.h>
#include <pbl/soc/t5ai/retained.h>

#include "console/dbgserial.h"

#include <inttypes.h>
#include <stdint.h>

PBL_LOG_MODULE_DEFINE(soc_t5ai_bootbits, LOG_LEVEL_DEBUG);

void boot_bit_init(void) {
  t5ai_retained_init();
  if (!boot_bit_test(BOOT_BIT_INITIALIZED)) {
    t5ai_retained_write(RTC_BKP_BOOTBIT_DR, BOOT_BIT_INITIALIZED);
  }
}

void boot_bit_set(BootBitValue bit) {
  t5ai_retained_write(RTC_BKP_BOOTBIT_DR,
                      t5ai_retained_read(RTC_BKP_BOOTBIT_DR) | bit);
  if (bit == BOOT_BIT_FW_STABLE) {
    PBL_LOG_DBG("Firmware stable bit readback: bits=0x%08" PRIx32 " ticks=%" PRIu32,
                boot_bits_get(), (uint32_t)pbl_uptime_ticks());
  }
}

void boot_bit_clear(BootBitValue bit) {
  if (bit == BOOT_BIT_RESET_LOOP_DETECT_ONE) {
    PBL_LOG_DBG("Startup boot bits: 0x%08" PRIx32, boot_bits_get());
  }
  t5ai_retained_write(RTC_BKP_BOOTBIT_DR,
                      t5ai_retained_read(RTC_BKP_BOOTBIT_DR) & ~bit);
}

bool boot_bit_test(BootBitValue bit) {
  return t5ai_retained_read(RTC_BKP_BOOTBIT_DR) & bit;
}

void boot_bit_dump(void) {
  PBL_LOG_DBG("0x%" PRIx32, t5ai_retained_read(RTC_BKP_BOOTBIT_DR));
}

uint32_t boot_bits_get(void) {
  return t5ai_retained_read(RTC_BKP_BOOTBIT_DR);
}

void command_boot_bits_get(void) {
  char buffer[32];
  dbgserial_putstr_fmt(buffer, sizeof(buffer), "bootbits: 0x%" PRIu32,
                       boot_bits_get());
}

uint32_t boot_version_read(void) {
  return 0;
}
