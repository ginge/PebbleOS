/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/mailbox_channel.h>

#include <logging/logging_private.h>

// The stock function is renamed only in this SoC's build, including with LTO.
LogState *t5ai_stock_get_log_state(void);

LogState *kernel_applib_get_log_state(void) {
  // Reject before the shared log-state mutex, and therefore before PULSE locks.
  if (t5ai_mailbox_channel_is_owner()) {
    return NULL;
  }
  return t5ai_stock_get_log_state();
}
