/* SPDX-License-Identifier: Apache-2.0 */
#include "tkl_ipc.h"

#include <stddef.h>
#include <stdint.h>

#include <driver/mailbox_channel.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>

const uint32_t unhygienix_cp_contract[] = {
#define CP_CONTRACT(field, expected, expression) expression,
#include "unhygienix_cp_contract.def"
#undef CP_CONTRACT
};
