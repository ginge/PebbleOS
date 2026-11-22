/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define ARCH_DCACHE_LINE_SIZE CONFIG_DCACHE_LINE_SIZE
#define ARCH_ICACHE_LINE_SIZE CONFIG_ICACHE_LINE_SIZE
void arch_dcache_flush_range(uintptr_t address, size_t size);
void arch_dcache_invalidate_range(uintptr_t address, size_t size);
void arch_dcache_flush_invalidate_range(uintptr_t address, size_t size);
void arch_icache_invalidate_range(uintptr_t address, size_t size);
