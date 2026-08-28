/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>

struct MpuRegion;
bool pebble_tuya_svc_dispatch(uint32_t *frame, uint32_t exc_return);
/* Array order defines vendor slots 5..7; Pebble logical region IDs are not reused. */
bool pebble_tuya_set_task_memory(TaskParameters_t *params,
                                 const struct MpuRegion *const regions[3]);
BaseType_t pebble_tuya_create_restricted_task(const TaskParameters_t *params, TaskHandle_t *handle);
