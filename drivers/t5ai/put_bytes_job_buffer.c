/* SPDX-License-Identifier: Apache-2.0 */
#include "pbl/services/put_bytes/put_bytes_job_buffer.h"

#include <string.h>

#define T5AI_PUT_BYTES_BUFFER_ALIGNMENT 32u

/* PutBytes starts after compositor_init() has powered PSRAM. These buffers are
 * private CPU staging for filesystem writes, so they do not participate in
 * the AP/CP mailbox cache-ownership protocol. Keep them out of the scarce and
 * fragmentation-sensitive internal kernel heap. */
static uint8_t s_job_buffers[MAX_BATCHED_PB_PUT_OPS][PUT_BYTES_JOB_BUFFER_CAPACITY]
    __attribute__((section(".t5ai_board_cache"), aligned(T5AI_PUT_BYTES_BUFFER_ALIGNMENT)));

uint8_t *put_bytes_job_buffer_alloc(size_t slot, size_t size) {
  if (slot >= MAX_BATCHED_PB_PUT_OPS || size > PUT_BYTES_JOB_BUFFER_CAPACITY) {
    return NULL;
  }
  memset(s_job_buffers[slot], 0, size);
  return s_job_buffers[slot];
}

void put_bytes_job_buffer_free(size_t slot, uint8_t *buffer) {
  if (slot < MAX_BATCHED_PB_PUT_OPS && buffer == s_job_buffers[slot]) {
    memset(s_job_buffers[slot], 0, PUT_BYTES_JOB_BUFFER_CAPACITY);
  }
}
