/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>

#include <pbl/drivers/audio/t5ai.h>
#include <pbl/drivers/dma/t5ai.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/cache.h>
#include <pbl/kernel/kernel.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/t5ai/core.h>
#include <pbl/soc/t5ai/cp_mailbox.h>
#include <pbl/soc/t5ai/delay.h>
#include <pbl/soc/t5ai/devices.h>
#include <pbl/soc/t5ai/rtc.h>
#include <pbl/util/misc.h>

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

#define DMA_HALF_TICKS (T5AI_AON_RTC_HZ / 50u)
#define DMA_BYTES      (T5AI_MIC_DMA_SAMPLES * sizeof(uint32_t))

static int16_t s_completed[T5AI_MIC_DMA_HALF_SAMPLES]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));

static const struct pbl_dma_chan s_dma = PBL_DMA_CHAN(&T5AI_DMA, 1, 0x0e, 0);
static void prv_dma_done(void *arg, int status);
static int prv_dma_start(const struct pbl_audio_t5ai *device) {
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

static volatile uint32_t *prv_reg(const struct pbl_audio_t5ai *device, uint32_t offset) {
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

static bool prv_power_down(const struct pbl_audio_t5ai *device) {
  struct pbl_audio_t5ai_state *state = device->state;
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

static bool prv_power_up(const struct pbl_audio_t5ai *device) {
  struct pbl_audio_t5ai_state *state = device->state;
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

static bool prv_dma_writing_second(const struct pbl_audio_t5ai_state *state) {
  (void)state;
  size_t residue;
  return pbl_dma_tx_status(&s_dma, &residue) == 0 && residue != 0 && residue <= DMA_BYTES / 2u;
}

static void prv_dma_done(void *arg, int status) {
  const struct pbl_audio_t5ai *device = arg;
  struct pbl_audio_t5ai_state *state = device->state;
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
    state->period_frames = 0;
    pbl_audio_report_xrun(pbl_audio_dai_card(&device->dai));
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
    state->period_frames = 0;
    pbl_audio_report_xrun(pbl_audio_dai_card(&device->dai));
  }
  uint32_t *words = state->dma_buffer + (writing_second ? 0u : T5AI_MIC_DMA_HALF_SAMPLES);
  pbl_dcache_invalidate_range(words, DMA_BYTES / 2u);
  for (unsigned i = 0; i < T5AI_MIC_DMA_HALF_SAMPLES; ++i)
    s_completed[i] = (int16_t)words[i]; // MIC1 low half; discard MIC2.
  __DSB();
  if (prv_dma_writing_second(state) != writing_second) {
    state->dropped_samples += T5AI_MIC_DMA_HALF_SAMPLES;
    state->period_frames = 0;
    pbl_audio_report_xrun(pbl_audio_dai_card(&device->dai));
    return;
  }
  const struct pbl_audio *audio = pbl_audio_dai_card(&device->dai);
  const struct pbl_audio_stream *stream = &audio->state->stream;
  for (unsigned i = 0; i < T5AI_MIC_DMA_HALF_SAMPLES; ++i) {
    int16_t *period = pbl_audio_period(audio, state->period_index);
    period[state->period_frames++] = s_completed[i];
    if (state->period_frames == stream->period_frames) {
      state->period_frames = 0;
      state->period_index = (state->period_index + 1u) % stream->periods;
      pbl_audio_period_elapsed(audio);
    }
  }
}

static const struct pbl_audio_t5ai *prv_dev(const struct pbl_audio_dai *dai) {
  return container_of(dai, const struct pbl_audio_t5ai, dai);
}

static int prv_init(const struct pbl_audio_dai *dai) {
  prv_dev(dai)->state->gain = AUD_ADC_GAIN_DEFAULT;
  return pbl_device_init(&T5AI_DMA.dev);
}

static void prv_caps(const struct pbl_audio_dai *dai, struct pbl_audio_caps *caps) {
  *caps = (struct pbl_audio_caps){
    .rates = PBL_AUDIO_RATE_16000,
    .min_channels = 1,
    .max_channels = 1,
    .gain_min_mdb = 0,
    .gain_max_mdb = 0,
    .gain_default_mdb = 0,
  };
}

static int prv_start(const struct pbl_audio_dai *dai, const struct pbl_audio_stream *stream,
                     int32_t gain_mdb, bool mute) {
  const struct pbl_audio_t5ai *device = prv_dev(dai);
  struct pbl_audio_t5ai_state *state = device->state;
  if (stream->rate != 16000 || stream->channels != 1 || !stream->period_frames ||
      stream->periods < 2 || gain_mdb != 0 || mute)
    return -EINVAL;
  if (state->running)
    return -EBUSY;
  if (state->faulted)
    return -EIO;
  state->dma_buffer = pbl_audio_buf_alloc(DMA_BYTES);
  if (!state->dma_buffer)
    return -ENOMEM;
  if (!prv_power_up(device)) {
    pbl_audio_buf_free(state->dma_buffer);
    state->dma_buffer = nullptr;
    return -EIO;
  }
  int ret = prv_dma_start(device);
  if (ret != 0) {
    (void)prv_power_down(device);
    pbl_audio_buf_free(state->dma_buffer);
    state->dma_buffer = nullptr;
    return ret;
  }
  state->period_index = 0;
  state->period_frames = 0;
  state->dropped_samples = state->fifo_full_events = 0;
  pbl_irq_lock();
  state->settle_deadline = t5ai_rtc_get_raw_ticks() + MIC_SETTLE_TICKS;
  state->running = true;
  state->dma_irq_ticks = t5ai_rtc_get_raw_ticks();
  *prv_reg(device, AUD_FIFO_CONFIG) = AUD_FIFO_THRESHOLD;
  *prv_reg(device, AUD_CONFIG) |= AUD_ADC_ENABLE;
  pbl_irq_unlock();
  return 0;
}

static int prv_stop(const struct pbl_audio_dai *dai) {
  const struct pbl_audio_t5ai *device = prv_dev(dai);
  struct pbl_audio_t5ai_state *state = device->state;
  pbl_irq_lock();
  state->running = false;
  pbl_irq_unlock();
  bool stopped = prv_power_down(device);
  pbl_audio_buf_free(state->dma_buffer);
  state->dma_buffer = nullptr;
  return stopped ? 0 : -EIO;
}

const struct pbl_audio_dai_ops pbl_audio_t5ai_ops = {
  .init = prv_init,
  .get_caps = prv_caps,
  .start = prv_start,
  .stop = prv_stop,
};
