/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "tuya_cloud_types.h"
typedef void *QUEUE_HANDLE;
typedef void *MUTEX_HANDLE;
OPERATE_RET tal_queue_create_init(QUEUE_HANDLE *, uint32_t, uint32_t);
OPERATE_RET tal_queue_post(QUEUE_HANDLE, const void *, uint32_t);
OPERATE_RET tal_queue_fetch(QUEUE_HANDLE, void *, uint32_t);
OPERATE_RET tal_mutex_create_init(MUTEX_HANDLE *);
OPERATE_RET tal_mutex_lock(MUTEX_HANDLE);
OPERATE_RET tal_mutex_unlock(MUTEX_HANDLE);
