/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pbl/drivers/display.h>
#include <pbl/soc/t5ai/graphics_fast.h>

#include <applib/graphics/gcontext.h>
#include <process_state/app_state/app_state.h>

[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  abort();
}
bool test_legacy;
bool process_manager_compiled_with_legacy2_sdk(void) {
  return test_legacy;
}
GContext *app_state_get_graphics_context(void) {
  return NULL;
}
GBitmap *graphics_context_get_bitmap(GContext *ctx) {
  assert(!ctx);
  static GBitmap other;
  return &other;
}
static const uint8_t mask[PBL_DISPLAY_HEIGHT / 2] = {0};
static const struct pbl_display display = {
  .width = PBL_DISPLAY_WIDTH, .height = PBL_DISPLAY_HEIGHT, .round_mask = mask
};
const struct pbl_display *const DISPLAY = &display;
void pbl_log(uint8_t level, const char *file, int line, const char *fmt, ...) {
  abort();
}
