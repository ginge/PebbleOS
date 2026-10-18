/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#include <system/rtc_registers.h>

#if defined(CONFIG_SOC_T5AI)
#include <pbl/soc/t5ai/retained.h>

static inline void retained_storage_init(void) {
  t5ai_retained_init();
}
static inline uint32_t retained_storage_read(uint32_t reg) {
  return t5ai_retained_read(reg);
}
static inline void retained_storage_write(uint32_t reg, uint32_t value) {
  t5ai_retained_write(reg, value);
}
static inline uint32_t retained_storage_boot_version_read(void) {
  return t5ai_boot_version_read();
}
#elif defined(CONFIG_SOC_NRF52)
static inline uint32_t retained_storage_read(uint32_t reg) {
  return retained_read(reg);
}
static inline void retained_storage_write(uint32_t reg, uint32_t value) {
  retained_write(reg, value);
}
#elif !defined(CONFIG_SOC_SF32LB52)
#include <pbl/drivers/rtc.h>

extern void RTC_WriteBackupRegister(uint32_t reg_id, uint32_t value);
extern uint32_t RTC_ReadBackupRegister(uint32_t reg_id);

#define retained_storage_init  rtc_init
#define retained_storage_read  RTC_ReadBackupRegister
#define retained_storage_write RTC_WriteBackupRegister
static inline uint32_t retained_storage_boot_version_read(void) {
  return RTC_ReadBackupRegister(BOOTLOADER_VERSION_REGISTER);
}
#endif
