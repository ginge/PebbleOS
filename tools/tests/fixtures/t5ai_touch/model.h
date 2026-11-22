/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "soc/t5ai/touch_queue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/kernel.h>
#include <pbl/soc/t5ai/rtc.h>

#include <applib/ui/recognizer/pan.h>
#include <applib/ui/recognizer/recognizer_private.h>
#include <kernel/events.h>
#include <kernel/pebble_tasks.h>
#include <process_management/app_manager.h>
extern struct pbl_msgq input_queue, app_queue;
extern PebbleProcessMd metadata;
extern struct pbl_thread app_thread;
extern PebbleTask task;
extern uint64_t clock_ms, delivered_ticks;
extern unsigned control, irq_depth, callback_tokens;
extern Recognizer *pan;
void setup(unsigned capacity);
extern void (*on_unlock)(void);
