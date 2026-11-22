/* SPDX-License-Identifier: Apache-2.0 */
#include "soc/t5ai/draw_profile.c"

#include <assert.h>

#include <sys/mman.h>

static struct pbl_thread app;
struct pbl_thread *pbl_cur = &app;
static unsigned locks, expected_key, calls;
unsigned test_control(void) {
  return 0;
}
unsigned test_ipsr(void) {
  return 0;
}
struct pbl_thread *pebble_task_get_thread(PebbleTask task) {
  assert(task == PebbleTask_App);
  return &app;
}
void pbl_irq_lock(void) {
  ++locks;
}
void pbl_irq_unlock(void) {
  assert(locks);
  --locks;
}
int __real_sched_block(struct pbl_waitq *queue, pbl_timeout_t timeout, unsigned int key) {
  assert(!queue && timeout.ticks == 7 && key == expected_key && !locks);
  ++calls;
  app.backend.run_time += 5;
  T5AI_AON_RTC_COUNT_LOW += 3;
  return 23;
}
struct pbl_heap *pbl_malloc_heap(void) {
  assert(false);
  return NULL;
}
size_t pbl_heap_min_headroom(const struct pbl_heap *heap) {
  assert(false);
  return 0;
}
void pbl_heap_stats(struct pbl_heap *heap, unsigned *used, unsigned *free_bytes,
                    unsigned *largest) {
  assert(false);
}
void pbl_thread_sleep(pbl_timeout_t timeout) {
  assert(false);
}
void t5ai_compositor_fast_dump(void) {
  assert(false);
}
int main(void) {
  void *registers = mmap((void *)0x44000000, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  assert(registers == (void *)0x44000000);
  for (unsigned active = 0; active < 3; ++active) {
    s_ready = active != 0;
    s_storage_ready = true;
    s_render_depth = active == 2;
    expected_key = 0x5a00u + active;
    assert(__wrap_sched_block(NULL, PBL_TICKS(7), expected_key) == 23);
  }
  assert(calls == 3 && !locks);
  assert(s_counts[5].fn == 5 && s_counts[5].count == 1 && s_counts[5].wall == 3 &&
         s_counts[5].cpu == 5);
  assert(munmap(registers, 4096) == 0);
  return 0;
}
