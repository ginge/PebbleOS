/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

#define T5AI_VOICE_ARENA_BYTES (16u * 1024u)

void *t5ai_voice_malloc_check(size_t size);
void *t5ai_voice_zalloc_check(size_t size);
void *t5ai_voice_realloc(void *ptr, size_t size);
void t5ai_voice_free(void *ptr);
