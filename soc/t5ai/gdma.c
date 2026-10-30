/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/irq.h>
#include <pbl/soc/t5ai/gdma.h>

#include <cmsis_core.h>

#define SYSTEM_CPU1_INTERRUPT_ENABLE (*(volatile uint32_t *)0x44010088u)
#define SYSTEM_GDMA_INTERRUPT        (1u << 11)

static uint32_t s_owners;
static bool s_inherited_nvic;
static bool s_inherited_route;

void t5ai_gdma_irq_acquire(T5aiGdmaOwner owner) {
  pbl_irq_lock();
  if (s_owners == 0) {
    s_inherited_nvic = NVIC_GetEnableIRQ(GDMA_IRQn) != 0;
    s_inherited_route = (SYSTEM_CPU1_INTERRUPT_ENABLE & SYSTEM_GDMA_INTERRUPT) != 0;
    SYSTEM_CPU1_INTERRUPT_ENABLE |= SYSTEM_GDMA_INTERRUPT;
    NVIC_SetPriority(GDMA_IRQn, 6u);
    NVIC_EnableIRQ(GDMA_IRQn);
  }
  s_owners |= owner;
  pbl_irq_unlock();
}

void t5ai_gdma_irq_release(T5aiGdmaOwner owner) {
  pbl_irq_lock();
  if ((s_owners & owner) != 0) {
    s_owners &= ~owner;
    if (s_owners == 0) {
      if (!s_inherited_nvic) {
        NVIC_DisableIRQ(GDMA_IRQn);
      }
      if (!s_inherited_route) {
        SYSTEM_CPU1_INTERRUPT_ENABLE &= ~SYSTEM_GDMA_INTERRUPT;
      }
    }
  }
  pbl_irq_unlock();
}
