/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rng.h>

#include <pbl/soc/t5ai/delay.h>

#include <cmsis_core.h>

#include <stddef.h>
#include <stdint.h>

/* BK7258 AP TRNG registers from the pinned T5AI BSP. */
#define T5AI_TRNG_BASE 0x458c0000u
#define T5AI_TRNG_GLOBAL_CTRL (*(volatile uint32_t *)(T5AI_TRNG_BASE + 0x08u))
#define T5AI_TRNG_CTRL (*(volatile uint32_t *)(T5AI_TRNG_BASE + 0x10u))
#define T5AI_TRNG_DATA (*(volatile uint32_t *)(T5AI_TRNG_BASE + 0x14u))

#define T5AI_TRNG_SOFT_RESET (1u << 0)
#define T5AI_TRNG_CLOCK_GATE_BYPASS (1u << 1)
#define T5AI_TRNG_ENABLE (1u << 0)
#define T5AI_TRNG_SETTLE_US 50u
#define T5AI_TRNG_STARTUP_READS 16u

bool rng_rand(uint32_t *rand_out) {
  if (rand_out == NULL) {
    return false;
  }

  /* The peripheral has one shared data stream. Keep a complete sample atomic. */
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();

  T5AI_TRNG_GLOBAL_CTRL |=
      T5AI_TRNG_SOFT_RESET | T5AI_TRNG_CLOCK_GATE_BYPASS;
  T5AI_TRNG_CTRL |= T5AI_TRNG_ENABLE;
  delay_us(T5AI_TRNG_SETTLE_US);

  for (unsigned i = 0; i < T5AI_TRNG_STARTUP_READS; ++i) {
    (void)T5AI_TRNG_DATA;
  }
  *rand_out = T5AI_TRNG_DATA;

  T5AI_TRNG_CTRL &= ~T5AI_TRNG_ENABLE;
  T5AI_TRNG_GLOBAL_CTRL &= ~T5AI_TRNG_CLOCK_GATE_BYPASS;

  if (primask == 0) {
    __enable_irq();
  }
  return true;
}
