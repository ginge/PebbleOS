/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODEL_CARD_BYTES (35u * 1024u * 1024u)
extern uint8_t model_card[MODEL_CARD_BYTES];
extern unsigned model_reads, model_writes, model_heap_allocs, model_heap_frees;
extern unsigned model_flags_heap_allocs, model_workspace_depth;
extern unsigned model_nor_erases;
extern int model_fault;
extern uint64_t model_capacity;

extern bool model_refusal_only;
