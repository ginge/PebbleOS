/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

//! Returns the unscaled 32 kHz always-on counter for hardware timebase validation.
uint64_t t5ai_rtc_get_raw_ticks(void);
