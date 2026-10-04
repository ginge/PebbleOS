/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Elect the active clock once and refresh SystemCoreClock from the clock tree.
// Call during T5AI startup before recalculating the scheduler tick period.
void t5ai_power_init(void);

void t5ai_power_update_clock(void);
