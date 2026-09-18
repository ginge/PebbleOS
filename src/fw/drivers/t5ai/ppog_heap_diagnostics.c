/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/ppog_heap_diagnostics.h>

#include "kernel/kernel_heap.h"

#include <pbl/logging/logging.h>
#include <pbl/util/heap.h>

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

void t5ai_ppog_heap_diagnostics_sample(const char *stage, size_t requested) {
  Heap *heap = kernel_heap_get();
  unsigned int used;
  unsigned int free_bytes;
  unsigned int largest_free;
  heap_calc_totals(heap, &used, &free_bytes, &largest_free);

  PBL_LOG_INFO("T5AI PPoG heap %s req=%u used=%u free=%u largest=%u",
               stage, (unsigned)requested, used, free_bytes, largest_free);
  PBL_LOG_INFO("T5AI PPoG heap state total=%u min=%lu",
               (unsigned)heap_size(heap),
               (unsigned long)heap_get_minimum_headroom(heap));
}
