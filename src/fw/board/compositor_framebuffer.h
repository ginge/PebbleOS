/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "applib/graphics/framebuffer.h"

extern FrameBuffer g_board_compositor_framebuffer;

//! Prepare board-owned framebuffer storage before the common compositor
//! initializes or clears it.
void board_compositor_framebuffer_prepare(void);
