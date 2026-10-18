/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "system/status_codes.h"

#include <pbl/soc/t5ai/mailbox_protocol.h>

typedef struct {
  uint32_t header;
  uint32_t parameter1;
  uint32_t parameter2;
  uint32_t parameter3;
} T5AIMailboxMessage;

typedef void (*T5AIMailboxReceiveHandler)(T5AIMailboxMessage *message, void *context);
typedef void (*T5AIMailboxTransmitHandler)(const T5AIMailboxMessage *ack, status_t status,
                                           void *context);

/* Starts the CPU1 mailbox owner task and installs the reset handshake used to
 * retire a CP transaction which survived an AP reset. */
status_t t5ai_mailbox_channel_init(void);
status_t t5ai_mailbox_channel_start(void);
/* Recheck owner work after CP runtime initialization. Does not wait. */
void t5ai_mailbox_channel_wake(void);

/* A receive handler is deliberately bounded: it may copy state and edit the
 * returned ACK, but must not wait or perform service work. */
status_t t5ai_mailbox_channel_register(uint8_t channel, T5AIMailboxReceiveHandler handler,
                                       void *context);
void t5ai_mailbox_channel_unregister(uint8_t channel);

/* Each logical channel owns one stable outbound slot, matching Tuya's
 * mailbox_channel contract. The async callback runs on the mailbox task. */
status_t t5ai_mailbox_channel_send_async(uint8_t channel, const T5AIMailboxMessage *message,
                                         T5AIMailboxTransmitHandler handler, void *context);
status_t t5ai_mailbox_channel_send(uint8_t channel, const T5AIMailboxMessage *message,
                                   T5AIMailboxMessage *ack, uint32_t timeout_ms);
bool t5ai_mailbox_channel_faulted(void);

/* Pre-scheduler control traffic has a separate polling path. Runtime users
 * must use the task-owned APIs above. */
status_t t5ai_mailbox_channel_bootstrap_send(uint8_t channel,
                                             const T5AIMailboxMessage *message,
                                             T5AIMailboxMessage *ack, uint32_t timeout_ms);
