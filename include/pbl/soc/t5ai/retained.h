/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
void t5ai_retained_init(void);
uint32_t t5ai_retained_read(uint8_t index);
void t5ai_retained_write(uint8_t index, uint32_t value);

uint32_t t5ai_boot_version_read(void);
