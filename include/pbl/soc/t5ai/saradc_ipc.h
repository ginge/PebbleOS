/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#include "system/status_codes.h"

//! Read a peak of raw 12-bit samples from the CP-owned SARADC channel.
status_t t5ai_saradc_read_peak(uint8_t channel, uint8_t samples,
                               uint16_t *peak);
