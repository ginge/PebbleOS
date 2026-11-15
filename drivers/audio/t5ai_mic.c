/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>

#include <pbl/drivers/audio.h>
#include <pbl/drivers/mic/t5ai/definitions.h>
#include <pbl/soc/t5ai/devices.h>

extern MicDevice *const T5AI_NATIVE_MIC;
static int16_t s_period[T5AI_MIC_RING_SAMPLES]
    __attribute__((section(".t5ai_board_cache"), aligned(32)));
static uint8_t s_idx;
static const struct pbl_audio_dai s_dai;
static void prv_samples(int16_t *samples, size_t count, void *arg) {
  const struct pbl_audio *audio = pbl_audio_dai_card(&s_dai);
  memcpy(pbl_audio_period(audio, s_idx), samples, count * sizeof(*samples));
  s_idx = (s_idx + 1u) % audio->state->stream.periods;
  pbl_audio_period_elapsed(audio);
}
static int prv_init(const struct pbl_audio_dai *dai) {
  mic_init(T5AI_NATIVE_MIC);
  return 0;
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
  if (stream->rate != 16000 || stream->channels != 1 ||
      stream->period_frames > T5AI_MIC_RING_SAMPLES || mute)
    return -EINVAL;
  s_idx = 0;
  return mic_start(T5AI_NATIVE_MIC, prv_samples, nullptr, s_period, stream->period_frames) ? 0
                                                                                           : -EIO;
}
static int prv_stop(const struct pbl_audio_dai *dai) {
  mic_stop(T5AI_NATIVE_MIC);
  return 0;
}
static const struct pbl_audio_dai_ops s_ops = {
  .init = prv_init,
  .get_caps = prv_caps,
  .start = prv_start,
  .stop = prv_stop,
};
PBL_AUDIO_DAI_STATE_DEFINE(s_dai);
static const struct pbl_audio_dai s_dai =
    PBL_AUDIO_DAI_INIT(s_dai, "t5ai-adc", PBL_AUDIO_CAPTURE, &s_ops, nullptr);
PBL_DEVICE_REGISTER(s_dai, &s_dai.dev);
PBL_AUDIO_CARD_DEFINE(T5AI_MIC, "t5ai-mic", &s_dai, nullptr, nullptr, 0, 0);
