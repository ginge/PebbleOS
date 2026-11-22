/* SPDX-License-Identifier: Apache-2.0 */
#include "flash_translation.h"
#include "model.h"
#include "pfs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <pbl/board/partitions.h>

int main(void) {
  memset(model_card, 0x6e, MODEL_CARD_BYTES);
  memset(model_card + PART_FILESYSTEM_OFFSET, 0xff, PART_FILESYSTEM_SIZE);
  assert(pfs_init(true) == S_SUCCESS);
  assert(model_heap_allocs && model_nor_erases && !model_reads && !model_writes);
  int fd = pfs_open("nor", OP_FLAG_WRITE, FILE_TYPE_STATIC, 9001);
  assert(fd >= S_SUCCESS);
  uint8_t input[9001], output[9001];
  for (size_t i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(i * 13u + 7u);
  assert(pfs_write(fd, input, sizeof(input)) == sizeof(input));
  pfs_close(fd);
  unsigned allocations = model_heap_allocs, frees = model_heap_frees;
  pfs_set_size(PART_FILESYSTEM_SIZE, false);
  assert(model_heap_allocs == allocations + 1 && model_heap_frees == frees + 1);
  assert(pfs_init(true) == S_SUCCESS);
  fd = pfs_open("nor", OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  assert(fd >= S_SUCCESS);
  assert(pfs_read(fd, output, sizeof(output)) == sizeof(output));
  assert(!memcmp(input, output, sizeof(input)));
  pfs_close(fd);
  uint8_t zero = 0;
  ftl_write(&zero, 1, PART_FILESYSTEM_SIZE - 1u);
  unsigned erased = model_nor_erases;
  pfs_format(true);
  assert(model_nor_erases == erased + 1);
  assert(model_card[PART_FILESYSTEM_OFFSET + PART_FILESYSTEM_SIZE - 1u] == 0xff);
  for (size_t i = 0; i < PART_FILESYSTEM_OFFSET; ++i)
    assert(model_card[i] == 0x6e);
  for (size_t i = PART_FILESYSTEM_OFFSET + PART_FILESYSTEM_SIZE; i < MODEL_CARD_BYTES; ++i)
    assert(model_card[i] == 0x6e);
  puts(
      "PASS production NOR PFS: heap-backed cache, rebuild, reopen, format and partition isolation");
}
