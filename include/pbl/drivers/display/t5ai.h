/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/drivers/display/display.h>

#include <stdint.h>

void display_co5300_set_brightness(uint8_t level);
void display_co5300_update_cpu(NextRowCallback nrcb, UpdateCompleteCallback uccb);
