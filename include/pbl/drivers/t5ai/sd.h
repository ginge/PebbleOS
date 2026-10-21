/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T5AI_SD_SECTOR_SIZE 512u
#define T5AI_SD_MAX_BLOCKS  8u

// Exclusive AP ownership; callers must not access the controller registers.
int t5ai_sd_init(void);
int t5ai_sd_read(uint32_t sector, void *buffer, uint32_t count);
int t5ai_sd_write(uint32_t sector, const void *buffer, uint32_t count);
uint64_t t5ai_sd_sector_count(void);
void t5ai_sd_irq(void);
void t5ai_sd_dma_irq(void);

// Recursive driver lock protects the shared DMA workspace across adapter RMW.
uint8_t *t5ai_sd_lock_workspace(void);
void t5ai_sd_unlock_workspace(void);
