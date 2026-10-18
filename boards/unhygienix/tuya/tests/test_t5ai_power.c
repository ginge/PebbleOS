/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include T5AI_POWER_SOURCE

uint32_t SystemCoreClock;
static unsigned votes;
static int vote_status;
static volatile uint32_t *mode = (void *)0x44010020;
static volatile uint32_t *cpu1 = (void *)0x44010014;

status_t t5ai_cp_mailbox_vote_cpu1_frequency(T5AICpuFrequency frequency) {
  assert(frequency == T5AICpuFrequency_240MHz);
  ++votes;
  return vote_status;
}

int main(void) {
  assert(mmap((void *)0x44010000, 4096, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != (void *)-1);
  // Normal active election: 480 MHz DPLL divided by two, CPU1 full speed.
  *mode = (3u << 4u) | 1u;
  *cpu1 = 1u << 4u;
  t5ai_power_init();
  assert(votes == 1 && SystemCoreClock == 240000000);
  // Reinitialization and long sequences of idle/active checks do not revote.
  for (unsigned i = 0; i < 1000; ++i) t5ai_power_init();
  assert(votes == 1 && SystemCoreClock == 240000000);
  // A successful minimum vote must not conceal another module's higher vote.
  *mode = 2u << 4u;
  t5ai_power_init();
  assert(votes == 1 && SystemCoreClock == 320000000);
  *mode = 3u << 4u;
  t5ai_power_init();
  assert(votes == 1 && SystemCoreClock == 480000000);
  *cpu1 = 0;
  assert(prv_core_clock_hz() == 240000000);
  *mode = 0;
  *cpu1 = 1u << 4u;
  assert(prv_core_clock_hz() == 26000000);
  *mode = 1u << 4u;
  assert(prv_core_clock_hz() == 0);
  // A missing reply does not establish the elected rate. Read the hardware
  // even on failure, and allow the next startup initialization to retry.
  s_initialized = false;
  vote_status = -1;
  *mode = (3u << 4u) | 3u;
  t5ai_power_init();
  assert(votes == 2 && !s_initialized && SystemCoreClock == 120000000);
  // CP might have applied the vote even if its response was lost.
  *mode = (3u << 4u) | 1u;
  t5ai_power_init();
  assert(votes == 3 && !s_initialized && SystemCoreClock == 240000000);
  vote_status = 0;
  t5ai_power_init();
  assert(votes == 4 && s_initialized);
  t5ai_power_init();
  assert(votes == 4);
}
