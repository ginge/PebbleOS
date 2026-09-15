/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#define T5AI_DISPLAY_PSRAM_BASE 0x60000000u
#define T5AI_DISPLAY_PSRAM_SIZE (128u * 1024u)

// Pebble's ARM_CM33 FreeRTOS port owns configurable MPU regions 4..7.
// The boot-stage map leaves fixed regions 8..9 active, so region 10 is the
// first fixed region available to the native Pebble image.
#define T5AI_DISPLAY_PSRAM_MPU_REGION 10u
