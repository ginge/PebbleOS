/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#ifdef CONFIG_T5AI_DISPLAY_PROFILE
void t5ai_display_profile_frame(uint32_t start, uint32_t elapsed, uint32_t hz,
                                uint32_t dma_failures);
#endif
