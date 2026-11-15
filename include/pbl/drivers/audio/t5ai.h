/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/audio.h>

#define T5AI_MIC_DMA_HALF_SAMPLES 320u
#define T5AI_MIC_DMA_SAMPLES      (2u * T5AI_MIC_DMA_HALF_SAMPLES)

struct pbl_audio_t5ai_state {
  uint32_t *dma_buffer;
  uint64_t dma_irq_ticks;
  uint64_t settle_deadline;
  size_t period_frames;
  uint32_t dropped_samples;
  uint32_t fifo_full_events;
  uint8_t period_index;
  uint8_t gain;
  bool running;
  bool powered;
  bool faulted;
};

struct pbl_audio_t5ai {
  struct pbl_audio_dai dai;
  struct pbl_audio_t5ai_state *state;
  uintptr_t base;
  uint8_t analog_gain;
};

extern const struct pbl_audio_dai_ops pbl_audio_t5ai_ops;
