/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "hardware_core.h"

typedef struct {
  void *context;
  int (*read)(void *context, uint8_t reg, uint8_t *data, size_t size);
  int (*write)(void *context, uint8_t reg, const uint8_t *data, size_t size);
} TuyaRtcBus;

/* Caller serializes each complete operation against all other bus users. */
bool tuya_encode_rtc(const TuyaDateTime *time, uint8_t registers[7]);
int tuya_rtc_set(const TuyaRtcBus *bus, const TuyaDateTime *time);
/* Daily alarm. This programs the RTC but does not configure MCU wake-up. */
int tuya_rtc_daily_alarm(const TuyaRtcBus *bus, uint8_t hour, uint8_t minute, uint8_t second);
int tuya_rtc_alarm_clear(const TuyaRtcBus *bus);
