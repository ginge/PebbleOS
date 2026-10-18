/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/services/boot_splash.h>
#include <pbl/services/boot_splash/platform.h>
#include <pbl/services/boot_splash/t5ai.h>
#include <pbl/services/compositor/compositor.h>

#include <applib/graphics/framebuffer.h>
#include <system/passert.h>

static bool s_ready;
static bool s_requested;
static bool s_stopped;
static bool s_borrowed;

bool boot_splash_platform_defer_start(void) {
  if (s_stopped) {
    return true;
  }
  if (!s_ready) {
    s_requested = true;
    return true;
  }
  return false;
}

void boot_splash_platform_cancel_start(void) {
  s_requested = false;
  s_stopped = true;
}

uint8_t *boot_splash_platform_alloc(size_t size) {
  FrameBuffer *fb = compositor_get_framebuffer();
  if (s_borrowed || size > sizeof(fb->buffer)) {
    return nullptr;
  }
  // Board initialization has validated PSRAM; compositor_init() has not run yet.
  s_borrowed = true;
  return fb->buffer;
}

void boot_splash_platform_free(uint8_t *buffer) {
  PBL_ASSERTN(s_borrowed && buffer == compositor_get_framebuffer()->buffer);
  s_borrowed = false;
}

void t5ai_boot_splash_render(void) {
  s_ready = true;
  if (s_requested) {
    s_requested = false;
    boot_splash_start();
  }
}
