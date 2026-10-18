/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

// Backends must support every slot and requests up to this capacity.
#define MAX_BATCHED_PB_PUT_OPS        3
#define PUT_BYTES_JOB_BUFFER_CAPACITY 2080u

// The caller owns slot assignment and synchronization. Allocation clears size bytes.
uint8_t *put_bytes_job_buffer_alloc(size_t slot, size_t size);
void put_bytes_job_buffer_free(size_t slot, uint8_t *buffer);
