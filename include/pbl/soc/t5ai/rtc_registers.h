#pragma once
#include <stdint.h>
#define RTC_BKP_BOOTBIT_DR                0
#define STUCK_BUTTON_REGISTER             1
#define BOOTLOADER_VERSION_REGISTER       2
#define CURRENT_TIME_REGISTER             3
#define CURRENT_INTERVAL_TICKS_REGISTER   4
#define REBOOT_REASON_REGISTER_1          5
#define REBOOT_REASON_STUCK_TASK_PC       6
#define REBOOT_REASON_STUCK_TASK_LR       7
#define REBOOT_REASON_STUCK_TASK_CALLBACK 8
#define RTC_BKP_FLASH_ERASE_PROGRESS      9
#define SLOT_OF_LAST_LAUNCHED_APP         10
#define RTC_TIMEZONE_ABBR_START           12
#define RTC_TIMEZONE_ABBR_END_TZID_DSTID  13
#define RTC_TIMEZONE_GMTOFFSET            14
#define RTC_TIMEZONE_DST_START            15
#define RTC_TIMEZONE_DST_END              16
#define T5AI_RTC_BASE_TICKS_LOW           20
#define T5AI_RTC_BASE_TICKS_HIGH          21


void retained_write(uint8_t id, uint32_t value);
uint32_t retained_read(uint8_t id);
