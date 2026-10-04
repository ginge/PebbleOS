/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/kernel/compiler.h>

#include <stdint.h>

#define T5AI_RESET_REASON_MAILBOX_BASE 0x50u
#define T5AI_RESET_REASON_PEBBLE_BASE 0x60u

uint8_t t5ai_reset_reason_get_ap(void);
void t5ai_reset_reason_set_ap(uint8_t reason);

/* Uses both BK7258 watchdog domains and the AON reset mask so CPU0, CPU1,
 * radio, mailbox, and their retained transaction state restart together. */
PBL_NORETURN void t5ai_force_whole_chip_reset(uint8_t reason);
