/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "board/compositor_framebuffer.h"

#include <pbl/soc/t5ai/cp_mailbox.h>
#include "system/passert.h"

#include <cmsis_core.h>

#include <stddef.h>
#include <stdint.h>

FrameBuffer g_board_compositor_framebuffer
    __attribute__((section(".t5ai_framebuffer"), aligned(32)));

static bool prv_framebuffer_ram_is_valid(void) {
  volatile uint32_t *const words = (volatile uint32_t *)&g_board_compositor_framebuffer;
  const size_t word_count = sizeof(g_board_compositor_framebuffer) / sizeof(*words);
  const uint32_t seed = 0x3ca5c3a5u;

  for (size_t i = 0; i < word_count; ++i) {
    words[i] = seed ^ (uint32_t)i;
  }
  __DSB();

  for (size_t i = 0; i < word_count; ++i) {
    if (words[i] != (seed ^ (uint32_t)i)) {
      return false;
    }
    words[i] = ~(seed ^ (uint32_t)i);
  }
  __DSB();

  for (size_t i = 0; i < word_count; ++i) {
    if (words[i] != ~(seed ^ (uint32_t)i)) {
      return false;
    }
  }
  return true;
}

void board_compositor_framebuffer_prepare(void) {
  (void)t5ai_cp_mailbox_enable_psram();
  PBL_ASSERTN(prv_framebuffer_ram_is_valid());
}
