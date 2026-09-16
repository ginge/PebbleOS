/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

uint8_t *put_bytes_job_buffer_alloc(size_t slot, size_t size);
void put_bytes_job_buffer_free(size_t slot, uint8_t *buffer);
