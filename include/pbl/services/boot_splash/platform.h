/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_BOOT_SPLASH_CUSTOM_PLATFORM
// Deferred requests must be cancelled by stop, even before the display is ready.
bool boot_splash_platform_defer_start(void);
void boot_splash_platform_cancel_start(void);
// A borrowed buffer is exclusively owned until release, including the final frame.
uint8_t *boot_splash_platform_alloc(size_t size);
void boot_splash_platform_free(uint8_t *buffer);
#else
#include <kernel/pbl_malloc.h>

static inline bool boot_splash_platform_defer_start(void) {
  return false;
}
static inline void boot_splash_platform_cancel_start(void) {
}
static inline uint8_t *boot_splash_platform_alloc(size_t size) {
  return kernel_malloc(size);
}
static inline void boot_splash_platform_free(uint8_t *buffer) {
  kernel_free(buffer);
}
#endif
