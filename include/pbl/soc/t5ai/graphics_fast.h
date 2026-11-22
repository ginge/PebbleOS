/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <applib/graphics/bitblt.h>

// Full native assignment when eligible; all other operations use stock bitblt.
void t5ai_compositor_bitmap_into_bitmap_tiled(GBitmap *dest, const GBitmap *src, GRect rect,
                                              GPoint offset, GCompOp mode, GColor tint);

// Compositor-only normal-memory clear; preserve the caller's full requested span.
void *t5ai_compositor_memset(void *dest, int value, size_t length);
