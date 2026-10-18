/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/soc/t5ai/retained.h>
#include <string.h>
#include "system/passert.h"
#define T5AI_RETAINED_WORDS 32u
#define T5AI_RETAINED_MAGIC 0x50355452u
typedef struct {
  uint32_t magic;
  uint32_t values[T5AI_RETAINED_WORDS];
  uint32_t checksum;
} T5AIRetainedState;
static T5AIRetainedState s_retained __attribute__((section(".t5ai_retained")));
static uint32_t prv_checksum(void) {
  uint32_t checksum = s_retained.magic;
  for (unsigned i = 0; i < T5AI_RETAINED_WORDS; ++i) {
    checksum = (checksum << 5) | (checksum >> 27);
    checksum ^= s_retained.values[i];
  }
  return checksum;
}
void t5ai_retained_init(void) {
  if (s_retained.magic != T5AI_RETAINED_MAGIC || s_retained.checksum != prv_checksum()) {
    memset(&s_retained, 0, sizeof(s_retained));
    s_retained.magic = T5AI_RETAINED_MAGIC;
    s_retained.checksum = prv_checksum();
  }
}
uint32_t t5ai_retained_read(uint8_t index) {
  PBL_ASSERTN(index < T5AI_RETAINED_WORDS);
  return s_retained.values[index];
}
void t5ai_retained_write(uint8_t index, uint32_t value) {
  PBL_ASSERTN(index < T5AI_RETAINED_WORDS);
  s_retained.values[index] = value;
  s_retained.checksum = prv_checksum();
}

uint32_t t5ai_boot_version_read(void) {
  return 0;
}
