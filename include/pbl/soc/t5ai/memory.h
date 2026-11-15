/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Shared buffers align to the 32-byte data-cache line for cache maintenance.
#define T5AI_SHARED_ALIGNMENT 32u
// Accepted IPC alias windows, with exclusive ends; these are not memory capacities.
#define T5AI_SHARED_SRAM_BEGIN  0x28000000u
#define T5AI_SHARED_SRAM_END    0x29000000u
#define T5AI_SHARED_PSRAM_BEGIN 0x60000000u
#define T5AI_SHARED_PSRAM_END   0x61000000u

#define T5AI_DISPLAY_PSRAM_BASE 0x60000000u
#define T5AI_DISPLAY_PSRAM_SIZE (128u * 1024u)

// Native AP owns this part of the vendor audio slab; CP heap starts at 0x60700000.
#define T5AI_PFS_FLAGS_PSRAM_BASE 0x60020000u
#define T5AI_PFS_FLAGS_PSRAM_SIZE 8192u

// AP-only staging inside the retired vendor audio slab, after SD metadata.
#define T5AI_DISPLAY_DMA_PSRAM_BASE 0x60024000u
#define T5AI_DISPLAY_DMA_PSRAM_SIZE (16u * 1024u)
