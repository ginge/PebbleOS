/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/mic/t5ai/definitions.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/rtc.h>
#include "board/board.h"
#include "kernel/pbl_malloc.h"
#include <pbl/soc/t5ai/delay.h>
#include "system/passert.h"
#include <pbl/soc/t5ai/kernel.h>

#include <inttypes.h>
#include <pbl/mcu/cache.h>

PBL_LOG_MODULE_DEFINE(driver_mic_t5ai, CONFIG_DRIVER_MIC_LOG_LEVEL);

// BK7258 AP aud_ll_macro_def.h and sys_ll.h in the pinned Tuya BSP.
#define AUD_RESET 0x08u
#define AUD_ADC_CONFIG 0x10u
#define AUD_FIFO_CONFIG 0x28u
#define AUD_FIFO_STATUS 0x38u
#define AUD_ADC_FIFO 0x44u
#define AUD_EXTEND_CONFIG 0x60u
#define AUD_CONFIG 0xc0u
#define AUD_ADC_EMPTY (1u << 14)
#define AUD_ADC_FULL (1u << 10)
#define AUD_ADC_ENABLE ((1u << 3) | (1u << 5))
#define AUD_ADC_GAIN_SHIFT 18u
#define AUD_ADC_GAIN_MAX 63u
#define AUD_ADC_GAIN_DEFAULT 63u
#define AUD_FIFO_THRESHOLD (8u << 15)
#define AUD_IRQ_DRAIN_LIMIT 64u
#define SYS_AUDIO_CLOCK_SOURCE (*(volatile uint32_t *)0x44010020u)
#define SYS_AUDIO_APLL (1u << 25)
#define SYS_CPU1_IRQ_ENABLE (*(volatile uint32_t *)0x44010088u)
#define SYS_DMA_IRQ (1u << 11)
#define SYS_ANALOG_BASE 0x44010100u
#define SYS_ANALOG_BUSY (*(volatile uint32_t *)0x440100e8u)
#define SYS_AUDIO_BIAS_INDEX 18u
#define SYS_MIC1_INDEX 19u
#define SYS_AUDIO_BIAS_DEFAULT 0x00bf8085u
#define SYS_MIC1_DEFAULT 0x81800007u
#define SYS_AUDIO_BIAS_ENABLE ((1u << 3) | (1u << 4) | (1u << 5))
#define SYS_MIC1_ENABLE (1u << 28)
#define SYS_MIC1_RESET (1u << 29)
#define SYS_MIC1_GAIN_SHIFT 15u
#define MIC_SETTLE_TICKS (T5AI_AON_RTC_HZ / 5u)

// AP DMA0 channel 1; display owns channel 0, CP allocates logical IDs 12..15.
#define DMA_BASE 0x45020000u
#define DMA_CHANNEL_BIT (1u << 1)
#define DMA_CHANNEL_BASE 0x80u
#define DMA_CTRL 0x00u
#define DMA_DEST 0x04u
#define DMA_SRC 0x08u
#define DMA_DEST_END 0x0cu
#define DMA_DEST_START 0x10u
#define DMA_SRC_END 0x14u
#define DMA_SRC_START 0x18u
#define DMA_REQ 0x1cu
#define DMA_WRITE_ADDR 0x2cu
#define DMA_STATUS 0x30u
#define DMA_FINISH (1u << 18)
#define DMA_HALF (1u << 19)
#define DMA_ERROR (1u << 20)
#define DMA_IRQ_STATUS (DMA_FINISH | DMA_HALF | DMA_ERROR)
#define DMA_CACHE_ALIGNMENT 32u
#define DMA_HALF_TICKS (T5AI_AON_RTC_HZ / 50u)
#define DMA_BYTES (T5AI_MIC_DMA_SAMPLES * sizeof(uint32_t))
#define DMA_CONTROL                                                                       \
  (((DMA_BYTES - 1u) << 16) | (2u << 4) | (2u << 6) | (1u << 1) | (1u << 2) | (1u << 3) | \
   (1u << 9) | (1u << 10) | (1u << 11))
#define DMA_REQUEST (0x0eu | (1u << 20) | (1u << 21) | (1u << 22))

static int16_t s_mono_ring[T5AI_MIC_RING_SAMPLES]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));

static volatile uint32_t *prv_dma(uint32_t offset) {
  return (volatile uint32_t *)((uintptr_t)DMA_BASE + DMA_CHANNEL_BASE + offset);
}

static void prv_dma_start(MicDevice *device) {
  // Channel ownership was checked before powering audio. Preserve display state.
  *(volatile uint32_t *)0x44010040u &= ~(1u << 4);  // Backup domain.
  *(volatile uint32_t *)0x44010038u &= ~(1u << 30);
  *(volatile uint32_t *)0x4401003cu &= ~(1u << 30);
  delay_us(10u);
  *(volatile uint32_t *)(DMA_BASE + 0x08u) |= 3u;
  *(volatile uint32_t *)(DMA_BASE + 0x10u) |= DMA_CHANNEL_BIT;
  *(volatile uint32_t *)(DMA_BASE + 0x14u) |= DMA_CHANNEL_BIT;
  *(volatile uint32_t *)(DMA_BASE + 0x28u) &= ~DMA_CHANNEL_BIT;  // Secure IRQ0.
  const uint32_t destination = (uint32_t)(uintptr_t)device->state->dma_buffer;
  const uint32_t source = (uint32_t)device->base + AUD_ADC_FIFO;
  *prv_dma(DMA_CTRL) = 0u;
  *prv_dma(DMA_DEST) = destination;
  *prv_dma(DMA_DEST_START) = destination;
  *prv_dma(DMA_DEST_END) = destination + DMA_BYTES;
  *prv_dma(DMA_SRC) = source;
  *prv_dma(DMA_SRC_START) = source;
  *prv_dma(DMA_SRC_END) = source + sizeof(uint32_t);
  *prv_dma(DMA_REQ) = DMA_REQUEST;
  *prv_dma(DMA_STATUS) = DMA_IRQ_STATUS;
  dcache_flush_invalidate(device->state->dma_buffer, DMA_BYTES);
  *prv_dma(DMA_CTRL) = DMA_CONTROL | 1u;
}

static void prv_dma_stop(void) {
  *prv_dma(DMA_CTRL) = 0u;
  __DSB();
  (void)*prv_dma(DMA_CTRL);
  *prv_dma(DMA_STATUS) = DMA_IRQ_STATUS;
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
  NVIC_DisableIRQ(device->irq);
  *prv_reg(device, AUD_CONFIG) &= ~AUD_ADC_ENABLE;
  prv_dma_stop();
  *prv_reg(device, AUD_FIFO_CONFIG) = 0u;
  pbl_irq_lock();
  SYS_CPU1_IRQ_ENABLE &= ~SYS_DMA_IRQ;
  pbl_irq_unlock();
  NVIC_ClearPendingIRQ(device->irq);
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
  *prv_reg(device, AUD_CONFIG) = 1u;  // 16 kHz, XTAL, converters stopped.
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
  pbl_mutex_lock(state->mutex, PBL_FOREVER);
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
    system_task_watchdog_feed();
  }
  pbl_irq_lock();
  state->pending = false;
  pbl_irq_unlock();
  pbl_mutex_unlock(state->mutex);
}

static bool prv_dma_writing_second(const MicDeviceState *state) {
  const uint32_t destination = (uint32_t)(uintptr_t)state->dma_buffer;
  const uint32_t position = *prv_dma(DMA_WRITE_ADDR);
  // At finish, hardware may expose loop_end before its next FIFO request.
  return position >= destination + DMA_BYTES / 2u && position < destination + DMA_BYTES;
}

void t5ai_mic_dma_irq_handler(MicDevice *device) {
  MicDeviceState *state = device->state;
  const uint32_t status = *prv_dma(DMA_STATUS) & DMA_IRQ_STATUS;
  if (!status) {
    return;
  }
  *prv_dma(DMA_STATUS) = status;
  __DSB();
  if (!state->running) {
    return;
  }

  const uint64_t now = t5ai_rtc_get_raw_ticks();
  const uint64_t elapsed = now - state->dma_irq_ticks;
  state->dma_irq_ticks = now;
  if (status & DMA_ERROR) {
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
  const uint32_t expected = writing_second ? DMA_HALF : DMA_FINISH;
  if ((status & (DMA_HALF | DMA_FINISH)) != expected || elapsed >= 2u * DMA_HALF_TICKS) {
    // Sticky flags cannot count arbitrary missed laps: report at least one lost
    // block and recover at the newest completed half, never replay stale audio.
    state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
  }
  if (!(status & expected)) {
    return;
  }
  if (state->count + T5AI_MIC_DMA_HALF_SAMPLES > T5AI_MIC_RING_SAMPLES) {
    state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
  } else {
    uint32_t *words = state->dma_buffer + (writing_second ? 0u : T5AI_MIC_DMA_HALF_SAMPLES);
    dcache_invalidate(words, DMA_BYTES / 2u);
    size_t write_index = state->write_index;
    for (unsigned i = 0; i < T5AI_MIC_DMA_HALF_SAMPLES; ++i) {
      state->ring[write_index] = (int16_t)words[i];  // MIC1 low half; discard MIC2.
      write_index = (write_index + 1u) % T5AI_MIC_RING_SAMPLES;
    }
    __DSB();
    if ((prv_dma_writing_second(state)) == writing_second &&
        !(*prv_dma(DMA_STATUS) & (DMA_HALF | DMA_FINISH))) {
      state->write_index = write_index;
      state->count += T5AI_MIC_DMA_HALF_SAMPLES;
    } else {
      state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
    }
  }
  if (state->count >= state->buffer_len && !state->pending) {
    state->pending = true;
    if (!system_task_add_callback_from_isr_droppable(prv_dispatch, (void *)device)) {
      state->pending = false;
    }
  }

}

void mic_init(MicDevice *device) {
  MicDeviceState *state = device->state;
  if (state->initialized) {
    return;
  }
  state->mutex = t5ai_mutex_create();
  PBL_ASSERTN(state->mutex);
  state->gain = AUD_ADC_GAIN_DEFAULT;
  NVIC_DisableIRQ(device->irq);
  NVIC_SetPriority(device->irq, device->irq_priority);
  state->initialized = true;
}

void mic_set_volume(MicDevice *device, uint16_t volume) {
  MicDeviceState *state = device->state;
  pbl_mutex_lock(state->mutex, PBL_FOREVER);
  if (!state->running) {
    state->gain = volume == (uint16_t)MIC_DEFAULT_VOLUME
                      ? AUD_ADC_GAIN_DEFAULT
                      : (volume > 100u ? 100u : volume) * AUD_ADC_GAIN_MAX / 100u;
  }
  pbl_mutex_unlock(state->mutex);
}

bool mic_start(MicDevice *device, MicDataHandlerCB handler, void *context, int16_t *buffer,
               size_t buffer_len) {
  MicDeviceState *state = device->state;
  if (!state->initialized || !handler || !buffer || !buffer_len ||
      buffer_len > T5AI_MIC_RING_SAMPLES) {
    return false;
  }
  pbl_mutex_lock(state->mutex, PBL_FOREVER);
  if (state->running || state->faulted || (*prv_dma(DMA_CTRL) & 1u)) {
    pbl_mutex_unlock(state->mutex);
    return false;
  }
  // Each half occupies whole cache lines; maintenance cannot touch the other half.
  state->dma_allocation = kernel_malloc(DMA_BYTES + DMA_CACHE_ALIGNMENT - 1u);
  state->dma_buffer = (uint32_t *)(((uintptr_t)state->dma_allocation + DMA_CACHE_ALIGNMENT - 1u) &
                                   ~(uintptr_t)(DMA_CACHE_ALIGNMENT - 1u));
  state->ring = s_mono_ring;
  if (!state->dma_allocation || !prv_power_up(device)) {
    kernel_free(state->dma_allocation);
    state->dma_allocation = NULL;
    state->dma_buffer = NULL;
    state->ring = NULL;
    pbl_mutex_unlock(state->mutex);
    return false;
  }
  prv_dma_start(device);
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
  SYS_CPU1_IRQ_ENABLE |= SYS_DMA_IRQ;
  *prv_reg(device, AUD_FIFO_CONFIG) = AUD_FIFO_THRESHOLD;
  NVIC_ClearPendingIRQ(device->irq);
  NVIC_EnableIRQ(device->irq);
  *prv_reg(device, AUD_CONFIG) |= AUD_ADC_ENABLE;
  pbl_irq_unlock();
  pbl_mutex_unlock(state->mutex);
  PBL_LOG_DBG("Microphone started: 16000 Hz mono, gain=%u", state->gain);
  return true;
}

void mic_stop(MicDevice *device) {
  MicDeviceState *state = device->state;
  pbl_mutex_lock(state->mutex, PBL_FOREVER);
  if (state->running) {
    pbl_irq_lock();
    NVIC_DisableIRQ(device->irq);
    state->running = false;
    pbl_irq_unlock();
    (void)prv_power_down(device);
    kernel_free(state->dma_allocation);
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
  pbl_mutex_unlock(state->mutex);
}

bool mic_is_running(MicDevice *device) {
  pbl_mutex_lock(device->state->mutex, PBL_FOREVER);
  const bool running = device->state->running;
  pbl_mutex_unlock(device->state->mutex);
  return running;
}

uint32_t mic_get_channels(MicDevice *device) {
  return 1u;
}
