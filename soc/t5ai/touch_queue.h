/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "kernel/events.h"

#include <stdbool.h>
#include <stdint.h>

// Independent capacity for owned samples, not a copy of a native queue layout.
#define T5AI_TOUCH_SAMPLE_CAPACITY 96u

typedef struct {
  TouchEvent event;
  uint64_t ticks;
  struct pbl_msgq *queue;
  uint32_t token;
  uint32_t app_id;
  PebbleTaskBitset task_mask;
  bool app;
} T5AITouchSample;

uint64_t t5ai_touch_sample_ticks(void);
