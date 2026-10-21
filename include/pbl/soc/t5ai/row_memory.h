/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

// Non-overlapping normal memory only; never use for peripheral apertures.
typedef uint32_t T5RowWord __attribute__((may_alias));
static __attribute__((noinline, optimize("O3", "no-tree-loop-distribute-patterns"))) void
t5_row_copy(void *dst, const void *src, size_t length) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  if ((((uintptr_t)d ^ (uintptr_t)s) & 3u) == 0u) {
    while (length && ((uintptr_t)d & 3u)) {
      *d++ = *s++;
      --length;
    }
    while (length >= 4u) {
      *(T5RowWord *)d = *(const T5RowWord *)s;
      d += 4u;
      s += 4u;
      length -= 4u;
    }
  }
  while (length--) {
    *d++ = *s++;
  }
}
static inline __attribute__((always_inline)) void t5_row_fill(void *dst, uint8_t value,
                                                              size_t length) {
  uint8_t *d = dst;
  while (length && ((uintptr_t)d & 3u)) {
    *d++ = value;
    --length;
  }
  const uint32_t word = (uint32_t)value * 0x01010101u;
  while (length >= 4u) {
    *(T5RowWord *)d = word;
    d += 4u;
    length -= 4u;
  }
  while (length--) {
    *d++ = value;
  }
}
