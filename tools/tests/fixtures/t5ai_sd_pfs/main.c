/* SPDX-License-Identifier: Apache-2.0 */
#include "model.h"
#include "soc/t5ai/sd_pfs.c"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <pbl/kernel/malloc.h>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static void outside_unchanged(void) {
  for (size_t i = 0; i < HEADER_SECTOR * 512u; ++i)
    assert(model_card[i] == 0x6e);
  for (size_t i = (HEADER_SECTOR + 1u) * 512u; i < FIRST_SECTOR * 512u; ++i)
    assert(model_card[i] == 0x6e);
  for (size_t i = (FIRST_SECTOR * 512u + VOLUME_SIZE); i < MODEL_CARD_BYTES; ++i)
    assert(model_card[i] == 0x6e);
}
static void good_header(void) {
  VolumeHeader h = {
    .version = 1,
    .size = VOLUME_SIZE,
    .first_sector = FIRST_SECTOR,
    .size_inverse = ~VOLUME_SIZE
  };
  memcpy(h.magic, "PBLSDPFS", 8);
  memcpy(model_card + HEADER_SECTOR * 512u, &h, sizeof(h));
}
static void expect_abort(unsigned scenario) {
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    switch (scenario) {
      case 0:
        ftl_read(s_work, 2, VOLUME_SIZE - 1u);
        break;
      case 1:
        model_fault = -5;
        ftl_write(s_work, 1, 17);
        break;
      case 2:
        model_capacity = FIRST_SECTOR + VOLUME_SIZE / 512u - 1u;
        __wrap_pfs_init(true);
        break;
      case 3:
        model_fault = -5;
        __wrap_pfs_init(true);
        break;
      case 4:
        pfs_page_flags_cache_free(s_page_flags);
        pfs_page_flags_cache_free(s_page_flags);
        break;
      case 5:
        pfs_page_flags_cache_free(s_page_flags);
        pfs_page_flags_cache_alloc(sizeof(s_page_flags) - 1u);
        break;
      case 6:
        pfs_page_flags_cache_alloc(sizeof(s_page_flags));
        break;
      case 7:
        model_fault = -5;
        ftl_read(s_work, 1024, 0);
        break;
    }
    _exit(0);
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}
int main(int argc, char **argv) {
  (void)argc;
  setrlimit(RLIMIT_CORE, &(struct rlimit){0, 0});
  memset(model_card, 0x6e, MODEL_CARD_BYTES);
  if (strcmp(argv[1], "guard") == 0) {
    const unsigned offsets[] = {0, 8, 12, 16, 20};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
      good_header();
      model_card[HEADER_SECTOR * 512u + offsets[i]] ^= 1u;
      pid_t child = fork();
      assert(child >= 0);
      if (!child) {
        model_refusal_only = true;
        __wrap_pfs_init(true);
        _exit(0);
      }
      int status;
      assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 42);
      assert(!model_writes);
      outside_unchanged();
    }
    return 0;
  }

  if (strcmp(argv[1], "existing") == 0) {
    good_header();
    memset(model_card + FIRST_SECTOR * 512u, 0xff, VOLUME_SIZE);
  }
  void *ordinary = pbl_malloc_check(sizeof(s_page_flags));
  pbl_free(ordinary);
  assert(__wrap_pfs_init(true) == S_SUCCESS);
  assert(ftl_get_size() == VOLUME_SIZE && s_page_flags_in_use);
  assert(!model_nor_erases && model_flags_heap_allocs == 1);
  assert(pfs_active());
  outside_unchanged();
  int fd = pfs_open("roundtrip", OP_FLAG_WRITE, FILE_TYPE_STATIC, 12001);
  assert(fd >= S_SUCCESS);
  uint8_t input[12001], output[12001];
  for (size_t i = 0; i < sizeof(input); ++i)
    input[i] = (uint8_t)(i * 17u + 3u);
  assert(pfs_write(fd, input, sizeof(input)) == sizeof(input));
  pfs_close(fd);
  // Rebuild the real production flags cache, independent of allocation order.
  ordinary = pbl_malloc_check(sizeof(s_page_flags));
  pbl_free(ordinary);
  pfs_set_size(0, false);
  assert(!s_page_flags_in_use);
  pfs_set_size(VOLUME_SIZE, false);
  assert(model_flags_heap_allocs == 2 && s_page_flags_in_use);
  assert(__wrap_pfs_init(true) == S_SUCCESS);
  assert(model_flags_heap_allocs == 2 && !model_nor_erases);
  fd = pfs_open("roundtrip", OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  assert(fd >= S_SUCCESS);
  assert(pfs_read(fd, output, sizeof(output)) == sizeof(output));
  assert(!memcmp(input, output, sizeof(input)));
  pfs_close(fd);
  // Real format must erase the full SD volume, including its last subsector.
  uint8_t zero = 0;
  ftl_write(&zero, 1, VOLUME_SIZE - 1u);
  pfs_format(true);
  ftl_read(output, 1, VOLUME_SIZE - 1u);
  assert(output[0] == 0xff);
  assert(!model_nor_erases);
  outside_unchanged();
  // Raw partial/multi-sector paths preserve NOR bitwise-write semantics.
  ftl_erase_sector(65536, 0);
  ftl_write(input, sizeof(input), 511);
  ftl_read(output, sizeof(output), 511);
  assert(!memcmp(input, output, sizeof(input)));
  memset(input, 0xff, sizeof(input));
  ftl_write(input, sizeof(input), 511);
  ftl_read(output, sizeof(output), 511);
  for (size_t i = 0; i < sizeof(output); ++i)
    assert(output[i] == (uint8_t)(i * 17u + 3u));
  ftl_erase_subsector(4096, 0);
  unsigned reads = model_reads;
  ftl_read(output, 1, 4096);
  ftl_read(output, 1, 4097);
  assert(model_reads == reads + 1);
  for (unsigned i = 0; i < 8; ++i)
    expect_abort(i);
  // Fill the real 32 MiB volume, then reclaim space through normal PFS GC.
  pfs_format(true);
  unsigned filled = 0;
  bool full = false;
  for (unsigned i = 0; i < 40u; ++i) {
    char name[24];
    snprintf(name, sizeof(name), "fill%u", i);
    int file = pfs_open(name, OP_FLAG_WRITE, FILE_TYPE_STATIC, 1024u * 1024u);
    if (file == E_OUT_OF_STORAGE) {
      full = true;
      break;
    }
    assert(file >= S_SUCCESS);
    pfs_close(file);
    ++filled;
  }
  assert(full && filled > 0);
  assert(pfs_open("extra", OP_FLAG_WRITE, FILE_TYPE_STATIC, 1024u * 1024u) == E_OUT_OF_STORAGE);
  assert(pfs_remove("fill0") == S_SUCCESS);
  int reclaimed = pfs_open("reclaimed", OP_FLAG_WRITE, FILE_TYPE_STATIC, 1024u * 1024u);
  assert(reclaimed >= S_SUCCESS);
  pfs_close(reclaimed);
  assert(!model_workspace_depth);
  outside_unchanged();
  printf("PASS production PFS: real cache, format, reopen, file, SD I/O and fail-stop guards\n");
}
