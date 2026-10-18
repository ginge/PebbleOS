/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "system/status_codes.h"

#include <stdbool.h>
#include <stdint.h>

/* Completion runs on the mailbox owner, or inline on a local submission failure.
 * It must not wait or start service work. */
typedef void (*T5AISaradcCompletion)(status_t status, uint16_t peak, void *context);
status_t t5ai_saradc_init(void);
/* One bounded asynchronous acquisition; E_BUSY if another is active. */
status_t t5ai_saradc_request(uint8_t channel, uint8_t samples, T5AISaradcCompletion callback,
                             void *context);

/* Snapshot only: safe on the completion owner, with no logging or IPC. */
typedef struct {
  status_t status;
  uint32_t elapsed_ms;
  uint32_t invalid_count;
  int16_t cp_status;
  uint8_t failed_command; // UINT8_MAX denotes connect.
  bool available;
} T5AISaradcDiagnostics;
void t5ai_saradc_get_diagnostics(T5AISaradcDiagnostics *diagnostics);
