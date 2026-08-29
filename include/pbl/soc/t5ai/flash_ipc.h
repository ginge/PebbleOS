/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

/*
 * Pebble-side boundary to the CP-owned flash server. Addresses are logical
 * flash offsets; the CP server owns controller access and 34:32 protection.
 */
status_t t5ai_flash_ipc_init(void);
status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length);
status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length);
status_t t5ai_flash_ipc_erase_4k(uint32_t offset);
