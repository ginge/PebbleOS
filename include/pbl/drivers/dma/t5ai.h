/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/drivers/dma.h>
#define T5AI_DMA_PERIPHERAL_INCREMENT (1u << 0)
#define T5AI_DMA_BURST_INC16          (1u << 1)
extern const struct pbl_dma_controller T5AI_DMA;
void pbl_dma_t5ai_irq_handler(void);
