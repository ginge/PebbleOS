/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <applib/graphics/text.h>
#include <applib/ui/window.h>

// Privileged App render scope; other callers render without caching.
void t5ai_text_node_cache_window_render(Window *window, GContext *ctx);

// Only text_node.c supplies this entry; layout must point to TextLayoutExtended.
GSize t5ai_text_node_cached_size(GContext *ctx, const char *text, GFont font, GRect box,
                                 GTextOverflowMode overflow, GTextAlignment alignment,
                                 GTextLayoutCacheRef layout);
