/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/types.h>
#include <pbl/soc/t5ai/sleep_protocol.h>

// Task context only; unknown CP ABI or a lost transaction fails closed.
status_t t5ai_native_alarm_init(void);
status_t t5ai_native_alarm_arm(uint32_t aon_ticks, uint32_t *generation);
status_t t5ai_native_alarm_cancel(uint32_t generation);
// Close CP service ownership after all post-wake validation queries.
status_t t5ai_native_alarm_finish_cleanup(uint32_t generation);
bool t5ai_native_alarm_fired(uint32_t generation, uint32_t *aon_tick);
bool t5ai_native_alarm_healthy(void);

// Mailbox receive context: scalar copy only, never blocks or sends.
void t5ai_native_alarm_notify(uint32_t generation, uint32_t aon_tick, uint32_t abi);
status_t t5ai_cp_mailbox_alarm_request(T5AIAlarmOperation operation, uint32_t generation,
                                       uint32_t aon_ticks, uint32_t *applied_generation,
                                       uint32_t *abi);

#ifdef CONFIG_T5AI_NATIVE_ALARM_TEST
void t5ai_native_alarm_test_start(void);
#endif

status_t t5ai_native_alarm_prepare_sleep(uint32_t aon_ticks, uint32_t *generation);
status_t t5ai_native_alarm_cp_query(T5AIAlarmOperation operation, uint32_t *value);
void t5ai_native_alarm_resume_notify(uint32_t generation, uint32_t aon_tick, uint32_t abi);
bool t5ai_native_alarm_woken(uint32_t generation);

// Matching deferred CP event proves the controller task has resumed.
bool t5ai_native_alarm_event_received(uint32_t generation);
