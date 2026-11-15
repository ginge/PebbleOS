/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/board.h"
#include "pbl/kernel/malloc.h"

#include <inttypes.h>

#include <pbl/drivers/dma/t5ai.h>
#include <pbl/drivers/mic/t5ai/definitions.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/cache.h>
#include <pbl/kernel/kernel.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/rtc.h>

PBL_LOG_MODULE_DEFINE(driver_mic_t5ai, CONFIG_DRIVER_AUDIO_LOG_LEVEL);

// BK7258 AP aud_ll_macro_def.h and sys_ll.h in the pinned Tuya BSP.
#define AUD_RESET              0x08u
#define AUD_ADC_CONFIG         0x10u
#define AUD_FIFO_CONFIG        0x28u
#define AUD_FIFO_STATUS        0x38u
#define AUD_ADC_FIFO           0x44u
#define AUD_EXTEND_CONFIG      0x60u
#define AUD_CONFIG             0xc0u
#define AUD_ADC_EMPTY          (1u << 14)
#define AUD_ADC_FULL           (1u << 10)
#define AUD_ADC_ENABLE         ((1u << 3) | (1u << 5))
#define AUD_ADC_GAIN_SHIFT     18u
#define AUD_ADC_GAIN_MAX       63u
#define AUD_ADC_GAIN_DEFAULT   63u
#define AUD_FIFO_THRESHOLD     (8u << 15)
#define AUD_IRQ_DRAIN_LIMIT    64u
#define SYS_AUDIO_CLOCK_SOURCE (*(volatile uint32_t *)0x44010020u)
#define SYS_AUDIO_APLL         (1u << 25)
#define SYS_ANALOG_BASE        0x44010100u
#define SYS_ANALOG_BUSY        (*(volatile uint32_t *)0x440100e8u)
#define SYS_AUDIO_BIAS_INDEX   18u
#define SYS_MIC1_INDEX         19u
#define SYS_AUDIO_BIAS_DEFAULT 0x00bf8085u
#define SYS_MIC1_DEFAULT       0x81800007u
#define SYS_AUDIO_BIAS_ENABLE  ((1u << 3) | (1u << 4) | (1u << 5))
#define SYS_MIC1_ENABLE        (1u << 28)
#define SYS_MIC1_RESET         (1u << 29)
#define SYS_MIC1_GAIN_SHIFT    15u
#define MIC_SETTLE_TICKS       (T5AI_AON_RTC_HZ / 5u)

#define DMA_CACHE_ALIGNMENT 32u
#define DMA_HALF_TICKS      (T5AI_AON_RTC_HZ / 50u)
#define DMA_BYTES           (T5AI_MIC_DMA_SAMPLES * sizeof(uint32_t))

static int16_t s_mono_ring[T5AI_MIC_RING_SAMPLES]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));

static const struct pbl_dma_chan s_dma = PBL_DMA_CHAN(&T5AI_DMA, 1, 0x0e, 0);
static void prv_dma_done(void *arg, int status);
static int prv_dma_start(MicDevice *device) {
  int ret = pbl_device_init(&T5AI_DMA.dev);
  const struct pbl_dma_slave_config cfg = {
    .src_addr = device->base + AUD_ADC_FIFO,
    .src_addr_width = PBL_DMA_SLAVE_BUSWIDTH_4_BYTES,
  };
  if (ret == 0)
    ret = pbl_dma_slave_config(&s_dma, &cfg);
  if (ret != 0)
    return ret;
  pbl_dcache_flush_invalidate_range(device->state->dma_buffer, DMA_BYTES);
  return pbl_dma_prep_dma_cyclic(&s_dma, device->state->dma_buffer, DMA_BYTES, DMA_BYTES / 2u,
                                 PBL_DMA_DEV_TO_MEM, prv_dma_done, (void *)device);
}
static void prv_dma_stop(void) {
  (void)pbl_dma_terminate(&s_dma);
}

static volatile uint32_t *prv_reg(MicDevice *device, uint32_t offset) {
  return (volatile uint32_t *)(device->base + offset);
}

static bool prv_analog_write(uint32_t index, uint32_t value) {
  *(volatile uint32_t *)((uintptr_t)SYS_ANALOG_BASE + index * 4u) = value;
  const uint64_t deadline = t5ai_rtc_get_raw_ticks() + T5AI_AON_RTC_HZ / 100u;
  while (SYS_ANALOG_BUSY & (1u << index)) {
    if (t5ai_rtc_get_raw_ticks() >= deadline) {
      return false;
    }
  }
  return true;
}

static bool prv_power_down(MicDevice *device) {
  MicDeviceState *state = device->state;
  if (!state->powered) {
    return true;
  }
  *prv_reg(device, AUD_CONFIG) &= ~AUD_ADC_ENABLE;
  prv_dma_stop();
  *prv_reg(device, AUD_FIFO_CONFIG) = 0u;
  const bool mic_off = prv_analog_write(SYS_MIC1_INDEX, SYS_MIC1_DEFAULT);
  const bool bias_off = prv_analog_write(SYS_AUDIO_BIAS_INDEX, SYS_AUDIO_BIAS_DEFAULT);
  const status_t clock = t5ai_cp_mailbox_set_audio_clock(false);
  const status_t power = t5ai_cp_mailbox_set_audio_power(false);
  state->powered = false;
  if (!mic_off || !bias_off || FAILED(clock) || FAILED(power)) {
    state->faulted = true;
    PBL_LOG_ERR("Microphone power-down failed: clock=%ld power=%ld", (long)clock, (long)power);
    return false;
  }
  return true;
}

static bool prv_power_up(MicDevice *device) {
  MicDeviceState *state = device->state;
  // Leave shared PLLs and their calibration alone. The vendor ADC supports XTAL.
  if (SYS_AUDIO_CLOCK_SOURCE & SYS_AUDIO_APLL) {
    PBL_LOG_ERR("Microphone requires the XTAL audio clock");
    return false;
  }
  if (FAILED(t5ai_cp_mailbox_set_audio_power(true))) {
    PBL_LOG_ERR("Microphone audio power vote failed");
    state->faulted = true;
    return false;
  }
  if (FAILED(t5ai_cp_mailbox_set_audio_clock(true))) {
    PBL_LOG_ERR("Microphone audio clock vote failed");
    (void)t5ai_cp_mailbox_set_audio_power(false);
    state->faulted = true;
    return false;
  }
  state->powered = true;
  *prv_reg(device, AUD_RESET) = 1u;
  *prv_reg(device, AUD_FIFO_CONFIG) = 0u;
  *prv_reg(device, AUD_CONFIG) = 1u; // 16 kHz, XTAL, converters stopped.
  *prv_reg(device, AUD_EXTEND_CONFIG) = 0u;
  *prv_reg(device, AUD_ADC_CONFIG) =
      (1u << 16) | (1u << 17) | ((uint32_t)state->gain << AUD_ADC_GAIN_SHIFT);
  if (!prv_analog_write(SYS_AUDIO_BIAS_INDEX, SYS_AUDIO_BIAS_DEFAULT | SYS_AUDIO_BIAS_ENABLE) ||
      !prv_analog_write(SYS_MIC1_INDEX,
                        SYS_MIC1_DEFAULT | SYS_MIC1_ENABLE | SYS_MIC1_RESET |
                            ((uint32_t)device->analog_gain << SYS_MIC1_GAIN_SHIFT))) {
    (void)prv_power_down(device);
    return false;
  }
  delay_us(10u);
  if (!prv_analog_write(SYS_MIC1_INDEX,
                        SYS_MIC1_DEFAULT | SYS_MIC1_ENABLE |
                            ((uint32_t)device->analog_gain << SYS_MIC1_GAIN_SHIFT))) {
    (void)prv_power_down(device);
    return false;
  }
  // Discard any FIFO words left by an earlier session, with a bounded drain.
  for (unsigned i = 0; i < AUD_IRQ_DRAIN_LIMIT; ++i) {
    if (*prv_reg(device, AUD_FIFO_STATUS) & AUD_ADC_EMPTY) {
      break;
    }
    (void)*prv_reg(device, AUD_ADC_FIFO);
  }
  return true;
}

static void prv_dispatch(void *context) {
  MicDevice *device = context;
  MicDeviceState *state = device->state;
  pbl_mutex_lock(&state->mutex, PBL_FOREVER);
  // Bound encoder work so other KernelBG callbacks can run.
  for (unsigned frame = 0; frame < 2u && state->running; ++frame) {
    pbl_irq_lock();
    if (state->count < state->buffer_len) {
      pbl_irq_unlock();
      break;
    }
    size_t read_index = state->read_index;
    pbl_irq_unlock();
    // Keep these samples counted until copied so the ISR cannot overwrite them.
    // Stop is serialized by the mutex; only this consumer advances read_index.
    for (size_t i = 0; i < state->buffer_len; ++i) {
      state->buffer[i] = state->ring[read_index];
      read_index = (read_index + 1u) % T5AI_MIC_RING_SAMPLES;
    }
    pbl_irq_lock();
    state->read_index = read_index;
    state->count -= state->buffer_len;
    pbl_irq_unlock();
    state->handler(state->buffer, state->buffer_len, state->context);
  }
  pbl_irq_lock();
  state->pending = false;
  pbl_irq_unlock();
  pbl_mutex_unlock(&state->mutex);
}

static bool prv_dma_writing_second(const MicDeviceState *state) {
  (void)state;
  size_t residue;
  return pbl_dma_tx_status(&s_dma, &residue) == 0 && residue != 0 && residue <= DMA_BYTES / 2u;
}

static void prv_dma_done(void *arg, int status) {
  MicDevice *device = arg;
  MicDeviceState *state = device->state;
  if (!state->running) {
    return;
  }

  const uint64_t now = t5ai_rtc_get_raw_ticks();
  const uint64_t elapsed = now - state->dma_irq_ticks;
  state->dma_irq_ticks = now;
  if (status != 0) {
    // Stop writes immediately. The caller's normal stop path releases power/RAM.
    *prv_reg(device, AUD_CONFIG) &= ~AUD_ADC_ENABLE;
    prv_dma_stop();
    state->faulted = true;
    state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
    return;
  }
  if (now < state->settle_deadline + DMA_HALF_TICKS) {
    return;
  }
  if (*prv_reg(device, AUD_FIFO_STATUS) & AUD_ADC_FULL) {
    ++state->fifo_full_events;
  }
  // The half not currently being written is the only safe completed block.
  const bool writing_second = prv_dma_writing_second(state);
  if (elapsed >= 2u * DMA_HALF_TICKS) {
    // Sticky flags cannot count arbitrary missed laps: report at least one lost
    // block and recover at the newest completed half, never replay stale audio.
    state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
  }
  if (state->count + T5AI_MIC_DMA_HALF_SAMPLES > T5AI_MIC_RING_SAMPLES) {
    state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
  } else {
    uint32_t *words = state->dma_buffer + (writing_second ? 0u : T5AI_MIC_DMA_HALF_SAMPLES);
    pbl_dcache_invalidate_range(words, DMA_BYTES / 2u);
    size_t write_index = state->write_index;
    for (unsigned i = 0; i < T5AI_MIC_DMA_HALF_SAMPLES; ++i) {
      state->ring[write_index] = (int16_t)words[i]; // MIC1 low half; discard MIC2.
      write_index = (write_index + 1u) % T5AI_MIC_RING_SAMPLES;
    }
    __DSB();
    if (prv_dma_writing_second(state) == writing_second) {
      state->write_index = write_index;
      state->count += T5AI_MIC_DMA_HALF_SAMPLES;
    } else {
      state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
    }
  }
  if (state->count >= state->buffer_len && !state->pending) {
    state->pending = true;
    if (!pbl_sys_work_try_call(prv_dispatch, (void *)device)) {
      state->pending = false;
    }
  }
}

void mic_init(MicDevice *device) {
  MicDeviceState *state = device->state;
  if (state->initialized) {
    return;
  }
  pbl_mutex_init(&state->mutex);
  state->gain = AUD_ADC_GAIN_DEFAULT;
  state->initialized = true;
}

void mic_set_volume(MicDevice *device, uint16_t volume) {
  MicDeviceState *state = device->state;
  pbl_mutex_lock(&state->mutex, PBL_FOREVER);
  if (!state->running) {
    state->gain = volume == (uint16_t)MIC_DEFAULT_VOLUME
                      ? AUD_ADC_GAIN_DEFAULT
                      : (volume > 100u ? 100u : volume) * AUD_ADC_GAIN_MAX / 100u;
  }
  pbl_mutex_unlock(&state->mutex);
}

bool mic_start(MicDevice *device, MicDataHandlerCB handler, void *context, int16_t *buffer,
               size_t buffer_len) {
  MicDeviceState *state = device->state;
  if (!state->initialized || !handler || !buffer || !buffer_len ||
      buffer_len > T5AI_MIC_RING_SAMPLES) {
    return false;
  }
  pbl_mutex_lock(&state->mutex, PBL_FOREVER);
  if (state->running || state->faulted) {
    pbl_mutex_unlock(&state->mutex);
    return false;
  }
  // Each half occupies whole cache lines; maintenance cannot touch the other half.
  state->dma_allocation = pbl_malloc(DMA_BYTES + DMA_CACHE_ALIGNMENT - 1u);
  state->dma_buffer = (uint32_t *)(((uintptr_t)state->dma_allocation + DMA_CACHE_ALIGNMENT - 1u) &
                                   ~(uintptr_t)(DMA_CACHE_ALIGNMENT - 1u));
  state->ring = s_mono_ring;
  if (!state->dma_allocation || !prv_power_up(device)) {
    pbl_free(state->dma_allocation);
    state->dma_allocation = NULL;
    state->dma_buffer = NULL;
    state->ring = NULL;
    pbl_mutex_unlock(&state->mutex);
    return false;
  }
  if (prv_dma_start(device) != 0) {
    (void)prv_power_down(device);
    pbl_free(state->dma_allocation);
    state->dma_allocation = NULL;
    state->dma_buffer = NULL;
    state->ring = NULL;
    pbl_mutex_unlock(&state->mutex);
    return false;
  }
  state->handler = handler;
  state->context = context;
  state->buffer = buffer;
  state->buffer_len = buffer_len;
  state->read_index = state->write_index = state->count = 0u;
  state->dropped_samples = state->fifo_full_events = 0u;
  pbl_irq_lock();
  state->settle_deadline = t5ai_rtc_get_raw_ticks() + MIC_SETTLE_TICKS;
  state->running = true;
  state->dma_irq_ticks = t5ai_rtc_get_raw_ticks();
  *prv_reg(device, AUD_FIFO_CONFIG) = AUD_FIFO_THRESHOLD;
  *prv_reg(device, AUD_CONFIG) |= AUD_ADC_ENABLE;
  pbl_irq_unlock();
  pbl_mutex_unlock(&state->mutex);
  PBL_LOG_DBG("Microphone started: 16000 Hz mono, gain=%u", state->gain);
  return true;
}

void mic_stop(MicDevice *device) {
  MicDeviceState *state = device->state;
  pbl_mutex_lock(&state->mutex, PBL_FOREVER);
  if (state->running) {
    pbl_irq_lock();
    state->running = false;
    pbl_irq_unlock();
    (void)prv_power_down(device);
    pbl_free(state->dma_allocation);
    state->dma_allocation = NULL;
    state->dma_buffer = NULL;
    state->ring = NULL;
    state->handler = NULL;
    state->context = NULL;
    state->buffer = NULL;
    state->buffer_len = state->count = 0u;
    PBL_LOG_DBG("Microphone stopped: dropped=%" PRIu32 " fifo_full=%" PRIu32,
                state->dropped_samples, state->fifo_full_events);
  }
  pbl_mutex_unlock(&state->mutex);
}

bool mic_is_running(MicDevice *device) {
  pbl_mutex_lock(&device->state->mutex, PBL_FOREVER);
  const bool running = device->state->running;
  pbl_mutex_unlock(&device->state->mutex);
  return running;
}

uint32_t mic_get_channels(MicDevice *device) {
  return 1u;
}
