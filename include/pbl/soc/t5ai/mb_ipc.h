/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "system/status_codes.h"

#include <stddef.h>
#include <stdint.h>

typedef struct T5AIMbIpcSocket T5AIMbIpcSocket;

status_t t5ai_mb_ipc_init(void);
status_t t5ai_mb_ipc_open(uint8_t client_port, uint8_t server_port, T5AIMbIpcSocket **socket);
status_t t5ai_mb_ipc_send(T5AIMbIpcSocket *socket, uint8_t command, const void *data, size_t length,
                          uint32_t timeout_ms);
status_t t5ai_mb_ipc_receive(T5AIMbIpcSocket *socket, uint8_t *command, void *data, size_t length,
                             uint32_t timeout_ms);
// AON timestamp before acknowledging the last received service completion.
uint32_t t5ai_mb_ipc_last_receive_aon(const T5AIMbIpcSocket *socket);
void t5ai_mb_ipc_discard(T5AIMbIpcSocket *socket);
void t5ai_mb_ipc_poison(T5AIMbIpcSocket *socket);

/* Nonblocking client operations. Completion runs on the mailbox owner, never an ISR.
 * Control payloads are limited to 256 bytes. Request/response storage must remain stable until completion, and until reset
 * after an ambiguous failure. Do not mix these with synchronous calls on a socket. */
typedef void (*T5AIMbIpcCompletion)(status_t status, void *context);
status_t t5ai_mb_ipc_open_async(uint8_t client_port, uint8_t server_port, T5AIMbIpcSocket **socket,
                                uint64_t deadline_ms, T5AIMbIpcCompletion callback, void *context);
status_t t5ai_mb_ipc_exchange_async(T5AIMbIpcSocket *socket, uint8_t command, const void *request,
                                    void *response, size_t length, uint64_t deadline_ms,
                                    T5AIMbIpcCompletion callback, void *context);
/* Mailbox-owner only. Returns milliseconds until progress/deadline, or UINT32_MAX. */
uint32_t t5ai_mb_ipc_async_poll(void);
