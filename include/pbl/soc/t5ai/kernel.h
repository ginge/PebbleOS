/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <pbl/kernel/kernel.h>
#include <string.h>
#include <errno.h>
#include "kernel/pbl_malloc.h"

static inline struct pbl_sem *t5ai_sem_create(unsigned limit, unsigned initial) {
  struct pbl_sem *s = kernel_malloc(sizeof(*s));
  if (s)
    pbl_sem_init(s, initial, limit);
  return s;
}
static inline struct pbl_mutex *t5ai_mutex_create(void) {
  struct pbl_mutex *m = kernel_malloc(sizeof(*m));
  if (m)
    pbl_mutex_init(m);
  return m;
}
static inline int t5ai_thread_create(pbl_thread_entry_t entry, const char *name, size_t stack_words,
                                     void *arg, pbl_prio_t prio, struct pbl_thread **out,
                                     void *stack) {
  struct pbl_thread *t = kernel_malloc(sizeof(*t));
  if (!t)
    return -ENOMEM;
  memset(t, 0, sizeof(*t));
  if (!stack)
    stack = kernel_malloc(stack_words * sizeof(uint32_t));
  if (!stack) {
    kernel_free(t);
    return -ENOMEM;
  }
  struct pbl_thread_attr attr = {.entry = entry,
                                 .name = name,
                                 .arg = arg,
                                 .prio = prio,
                                 .privileged = true,
                                 .stack = stack,
                                 .stack_size = stack_words * sizeof(uint32_t)};
  if (out)
    *out = t;
  return pbl_thread_create(t, &attr);
}
