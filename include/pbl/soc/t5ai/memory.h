/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Shared buffers align to the 32-byte data-cache line for cache maintenance.
#define T5AI_SHARED_ALIGNMENT 32u
// Accepted IPC alias windows, with exclusive ends; these are not memory capacities.
#define T5AI_SHARED_SRAM_BEGIN 0x28000000u
#define T5AI_SHARED_SRAM_END 0x29000000u
#define T5AI_SHARED_PSRAM_BEGIN 0x60000000u
#define T5AI_SHARED_PSRAM_END 0x61000000u

#define T5AI_DISPLAY_PSRAM_BASE 0x60000000u
#define T5AI_DISPLAY_PSRAM_SIZE (128u * 1024u)

// Pebble's native ARM kernel owns configurable MPU regions 4..7.
// The boot-stage map leaves fixed regions 8..9 active, so region 10 is the
// first fixed region available to the native Pebble image.
#define T5AI_DISPLAY_PSRAM_MPU_REGION 10u
