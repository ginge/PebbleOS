/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/drivers/mic.h>
#include <pbl/kernel/mutex.h>

#include <cmsis_core.h>

#define T5AI_MIC_DMA_HALF_SAMPLES 320u
#define T5AI_MIC_DMA_SAMPLES (2u * T5AI_MIC_DMA_HALF_SAMPLES)
#define T5AI_MIC_RING_SAMPLES (8u * T5AI_MIC_DMA_HALF_SAMPLES)

typedef struct {
  struct pbl_mutex mutex;
  int16_t *ring;
  void *dma_allocation;
  uint32_t *dma_buffer;
  uint64_t dma_irq_ticks;
  size_t read_index;
  size_t write_index;
  size_t count;
  MicDataHandlerCB handler;
  void *context;
  int16_t *buffer;
  size_t buffer_len;
  uint64_t settle_deadline;
  uint32_t dropped_samples;
  uint32_t fifo_full_events;
  uint8_t gain;
  bool initialized;
  bool running;
  bool pending;
  bool powered;
  bool faulted;
} MicDeviceState;

struct MicDevice {
  MicDeviceState *state;
  uintptr_t base;
  IRQn_Type irq;
  uint8_t irq_priority;
  uint8_t analog_gain;
};

void t5ai_mic_dma_irq_handler(MicDevice *device);
