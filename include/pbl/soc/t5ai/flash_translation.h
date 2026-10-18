/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * T5AI protected flash stores 32 logical data bytes in each 34-byte physical
 * unit.  The final two bytes are generated protection data and are not part of
 * the logical XIP address space.
 */
#define T5AI_FLASH_LOGICAL_UNIT_SIZE 32u
#define T5AI_FLASH_PHYSICAL_UNIT_SIZE 34u

bool t5ai_flash_logical_to_physical(uint32_t logical, uint32_t *physical);
bool t5ai_flash_physical_to_logical(uint32_t physical, uint32_t *logical);
bool t5ai_flash_physical_address_is_data(uint32_t physical);
