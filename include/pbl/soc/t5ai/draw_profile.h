/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#ifdef CONFIG_T5AI_DRAW_PROFILE
void t5ai_draw_profile_touch(bool down);
#else
static inline void t5ai_draw_profile_touch(bool down) {
  (void)down;
}
#endif
