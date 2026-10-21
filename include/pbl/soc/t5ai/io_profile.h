/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

#include <pbl/soc/t5ai/rtc.h>
enum {
  T5IO_TOUCH_QUEUE,
  T5IO_TOUCH_LOCK,
  T5IO_I2C_WAIT,
  T5IO_TOUCH_READ,
  T5IO_TOUCH_CALLBACK,
  T5IO_XIP_COPY,
  T5IO_CACHE_HIT,
  T5IO_CP_READ,
  T5IO_APP_QUEUE,
  T5IO_COALESCED,
  T5IO_WORKER_PREP,
  T5IO_WORKER_QUEUE,
  T5IO_WORKER_COMPLETE,
  T5IO_COUNT
};
void t5ai_io_profile_add(unsigned kind, uint32_t elapsed, uint32_t amount);
void t5ai_io_profile_dump(void);
#define T5_IO_BEGIN(name) const uint32_t name = T5AI_AON_RTC_COUNT_LOW
#define T5_IO_END(kind, name, amount) \
  t5ai_io_profile_add(kind, (uint32_t)(T5AI_AON_RTC_COUNT_LOW - (name)), amount)
#define T5_IO_ADD(kind, elapsed, amount) t5ai_io_profile_add(kind, elapsed, amount)
