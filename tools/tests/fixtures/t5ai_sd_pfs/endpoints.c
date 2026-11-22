/* SPDX-License-Identifier: Apache-2.0 */
#include "model.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pbl/board/partitions.h>
#include <pbl/drivers/flash.h>
#include <pbl/drivers/t5ai/sd.h>
#include <pbl/kernel/fatal.h>
#include <pbl/kernel/malloc.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/thread.h>
#include <pbl/kernel/time.h>
#include <pbl/task_wdt/task_wdt.h>

uint8_t model_card[MODEL_CARD_BYTES];
unsigned model_reads, model_writes, model_heap_allocs, model_heap_frees;
unsigned model_flags_heap_allocs, model_workspace_depth, model_nor_erases;
int model_fault;
bool model_refusal_only;
const struct pbl_flash *const FLASH = nullptr;
uint64_t model_capacity = MODEL_CARD_BYTES / T5AI_SD_SECTOR_SIZE;
static uint8_t workspace[T5AI_SD_MAX_BLOCKS * T5AI_SD_SECTOR_SIZE];

void *pbl_malloc_check(size_t bytes) {
  ++model_heap_allocs;
  if (bytes == 8192u)
    ++model_flags_heap_allocs;
  void *ptr = malloc(bytes);
  assert(ptr);
  return ptr;
}
void pbl_free(void *ptr) {
  ++model_heap_frees;
  free(ptr);
}
int pbl_mutex_lock_lr(struct pbl_mutex *m, pbl_timeout_t timeout, uintptr_t lr) {
  (void)lr;
  (void)timeout;
  ++m->count;
  return 0;
}
void pbl_mutex_unlock(struct pbl_mutex *m) {
  assert(m->count);
  --m->count;
}
void pbl_mutex_assert_held(const struct pbl_mutex *m, bool held) {
  assert((m->count != 0) == held);
}
void pbl_thread_yield(void) {
}
uint64_t pbl_uptime_ticks(void) {
  static uint64_t ticks;
  return ++ticks;
}
void pbl_task_wdt_feed_self(void) {
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  fprintf(stderr, "PANIC %s:%d\n", file, line);
  abort();
}
[[noreturn]] void pbl_panic_at(const char *file, int line, const char *fmt, ...) {
  if (model_refusal_only)
    _Exit(model_writes == 0 && model_workspace_depth == 0 ? 42 : 43);
  fprintf(stderr, "PANIC %s:%d ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  abort();
}
int t5ai_sd_init(void) {
  return model_fault;
}
uint64_t t5ai_sd_sector_count(void) {
  return model_capacity;
}
uint8_t *t5ai_sd_lock_workspace(void) {
  ++model_workspace_depth;
  return workspace;
}
void t5ai_sd_unlock_workspace(void) {
  assert(model_workspace_depth);
  --model_workspace_depth;
}
int t5ai_sd_read(uint32_t sector, void *out, uint32_t count) {
  if (model_fault)
    return model_fault;
  assert(model_workspace_depth && count && count <= T5AI_SD_MAX_BLOCKS);
  assert((uint64_t)sector + count <= model_capacity);
  memcpy(out, model_card + (size_t)sector * T5AI_SD_SECTOR_SIZE, count * T5AI_SD_SECTOR_SIZE);
  ++model_reads;
  return 0;
}
int t5ai_sd_write(uint32_t sector, const void *in, uint32_t count) {
  if (model_fault)
    return model_fault;
  assert(model_workspace_depth && count && count <= T5AI_SD_MAX_BLOCKS);
  assert((uint64_t)sector + count <= model_capacity);
  memcpy(model_card + (size_t)sector * T5AI_SD_SECTOR_SIZE, in, count * T5AI_SD_SECTOR_SIZE);
  ++model_writes;
  return 0;
}
#ifdef MODEL_NOR
static void nor_bounds(uint32_t address, size_t size) {
  assert(address >= PART_FILESYSTEM_OFFSET);
  assert(size <= PART_FILESYSTEM_SIZE &&
         address - PART_FILESYSTEM_OFFSET <= PART_FILESYSTEM_SIZE - size);
}
int pbl_flash_read(const struct pbl_flash *flash, uint32_t address, void *out, size_t size) {
  (void)flash;
  nor_bounds(address, size);
  memcpy(out, model_card + address, size);
  return 0;
}
int pbl_flash_write(const struct pbl_flash *flash, uint32_t address, const void *in, size_t size) {
  (void)flash;
  nor_bounds(address, size);
  for (size_t i = 0; i < size; ++i)
    model_card[address + i] &= ((const uint8_t *)in)[i];
  return 0;
}
#endif
int pbl_flash_erase(const struct pbl_flash *flash, uint32_t address, size_t size) {
  (void)flash;
  (void)address;
  (void)size;
  ++model_nor_erases;
#ifdef MODEL_NOR
  nor_bounds(address, size);
  memset(model_card + address, 0xff, size);
  return 0;
#else
  assert(false && "SD PFS format reached physical NOR");
  return -1;
#endif
}

void *pbl_malloc(size_t bytes) {
  return pbl_malloc_check(bytes);
}
char *pbl_strdup(const char *text) {
  size_t size = strlen(text) + 1u;
  char *copy = pbl_malloc_check(size);
  memcpy(copy, text, size);
  return copy;
}
void pbl_sleep_ms(uint32_t ms) {
  (void)ms;
}
