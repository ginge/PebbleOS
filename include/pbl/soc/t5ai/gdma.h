/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* AP DMA0 channels; CP logical channels 12..15 are not managed here.
 * Claims are idempotent and cover a driver's IRQ lifetime. Stop and acknowledge
 * the driver's own channels before release. Never clear shared NVIC pending. */
typedef enum {
  T5AI_GDMA_DISPLAY = 1u << 0,
  T5AI_GDMA_MIC = 1u << 1,
  T5AI_GDMA_SD = (1u << 2) | (1u << 3),
} T5aiGdmaOwner;

void t5ai_gdma_irq_acquire(T5aiGdmaOwner owner);
void t5ai_gdma_irq_release(T5aiGdmaOwner owner);
