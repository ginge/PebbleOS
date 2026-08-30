/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

typedef struct {
  uintptr_t address;
  size_t length;
} T5AICPMailboxFrame;

/* One owner for the AP<->CP physical mailbox FIFO. Callers hold the lock for
 * an entire logical transaction, not merely an individual frame. */
status_t t5ai_cp_mailbox_lock(void);
void t5ai_cp_mailbox_unlock(void);
status_t t5ai_cp_mailbox_send(const void *data, size_t length);
status_t t5ai_cp_mailbox_receive(T5AICPMailboxFrame *frame);
void t5ai_cp_mailbox_discard_pending(void);
