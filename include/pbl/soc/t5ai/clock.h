/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

typedef struct {
  uint32_t mode1;
  uint32_t cpu1_control;
  uint32_t measured_hz;
} T5AIClockDiagnostics;

/* Measure CPU1 against the independent 32 kHz always-on clock. This is a
 * bounded, boot-time diagnostic and takes approximately 10 ms. */
T5AIClockDiagnostics t5ai_clock_get_diagnostics(void);
