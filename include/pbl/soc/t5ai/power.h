/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Elect the active clock once and refresh SystemCoreClock from the clock tree.
// Call from board startup after scheduler/mailbox initialization.
// Synchronizes the running tick to the elected hardware clock.
void t5ai_power_init(void);

void t5ai_power_update_clock(void);

// ISR-safe: follow CP clock changes without logging or mailbox transactions.
void t5ai_power_sync_tick_clock(void);

#ifdef CONFIG_T5AI_TICKLESS_WFI
#include <stdint.h>
// Ordinary WFI only: zero if either CP vote or the observed clock is uncertain.
uint32_t t5ai_power_wfi_clock_min_hz(void);
#endif
