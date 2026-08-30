/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/temperature.h>

#include "console/prompt.h"

#include <stdint.h>

void temperature_init(void) {
}

int32_t temperature_read(void) {
  return INT32_MIN;
}

void command_temperature_read(void) {
  prompt_send_response("Temperature unavailable");
}
