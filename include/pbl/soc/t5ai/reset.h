/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <pbl/util/attributes.h>

/* Uses both BK7258 watchdog domains and the AON reset mask so CPU0, CPU1,
 * radio, mailbox, and their retained transaction state restart together. */
NORETURN t5ai_force_whole_chip_reset(void);
