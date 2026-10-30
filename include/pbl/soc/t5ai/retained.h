/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
// Word 22 was unused in the legacy 32-word store; zero keeps legacy boot behavior.
#define T5AI_RTC_EXTERNAL_UNTRUSTED       22u
#define T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE 0x52544321u

void t5ai_retained_init(void);
uint32_t t5ai_retained_read(uint8_t index);
void t5ai_retained_write(uint8_t index, uint32_t value);

uint32_t t5ai_boot_version_read(void);

// Atomic clock transaction; interrupted persistence uses the existing invalid-store fallback.
void t5ai_retained_write_clock(uint32_t epoch, uint64_t ticks);
