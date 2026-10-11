/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

/*
 * Pebble-side boundary to the CP-owned flash server. Addresses are logical
 * flash offsets; the CP server owns controller access and 34:32 protection.
 */
status_t t5ai_flash_ipc_init(void);
// False after an ambiguous transaction; no retry or reconnect may reuse it.
bool t5ai_flash_ipc_is_available(void);
status_t t5ai_flash_ipc_read(uint32_t offset, void *buffer, size_t length);
status_t t5ai_flash_ipc_write(uint32_t offset, const void *buffer, size_t length);
status_t t5ai_flash_ipc_erase_4k(uint32_t offset);

#ifdef CONFIG_T5AI_FLASH_PROFILE
/* Lock-free diagnostic samples; fields may span adjacent transactions. */
typedef struct {
  uint32_t count;
  uint32_t max_ms;
  uint32_t last_ms;
  uint32_t last_tick;
  uint32_t last_offset;
  uint32_t last_length;
} T5AIFlashOperationDiagnostics;

typedef struct {
  T5AIFlashOperationDiagnostics read;
  T5AIFlashOperationDiagnostics write;
  T5AIFlashOperationDiagnostics erase;
  uint32_t active_command;
  uint32_t active_tick;
} T5AIFlashDiagnostics;

void t5ai_flash_ipc_get_diagnostics(T5AIFlashDiagnostics *diagnostics);
#endif
