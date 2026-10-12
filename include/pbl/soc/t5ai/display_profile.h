/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#ifdef CONFIG_T5AI_DISPLAY_PROFILE
typedef struct {
  uint32_t rows, panel_rows, bytes, dma_submits, dma_frames, cpu_frames;
  uint64_t convert_cycles, copy_cycles, clean_cycles, next_cycles, command_cycles;
  uint64_t cpu_cycles, wait_cycles, kick_cycles, observed_cycles, lock_cycles;
  uint32_t wait_max_cycles;
} T5AITransferProfile;

void t5ai_display_profile_frame(uint32_t start, uint32_t elapsed, uint32_t hz,
                                uint32_t dma_failures, const T5AITransferProfile *transfer);
#endif
