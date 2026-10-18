/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include "pbl/services/put_bytes/put_bytes_job_buffer.h"

#include "kernel/pbl_malloc.h"

uint8_t *put_bytes_job_buffer_alloc(size_t slot, size_t size) {
  (void)slot;
  return kernel_zalloc(size);
}

void put_bytes_job_buffer_free(size_t slot, uint8_t *buffer) {
  (void)slot;
  kernel_free(buffer);
}
