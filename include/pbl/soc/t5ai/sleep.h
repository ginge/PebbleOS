/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  T5AISleepOwner_SD,
  T5AISleepOwner_Flash,
  T5AISleepOwner_I2C,
  T5AISleepOwner_DMA,
  T5AISleepOwner_UART,
  T5AISleepOwner_Mailbox,
  T5AISleepOwner_Count,
} T5AISleepOwner;

typedef struct {
  uint32_t epoch;
  uint32_t owners;
  bool faulted;
} T5AISleepBlockers;

void t5ai_sleep_fault(void);
void t5ai_sleep_block(T5AISleepOwner owner);
void t5ai_sleep_unblock(T5AISleepOwner owner);
T5AISleepBlockers t5ai_sleep_blockers(void);
// Called only with interrupts masked at the final idle check.
bool t5ai_sleep_blockers_match(uint32_t epoch);

typedef enum {
  T5AISleepFault_Contract = 1,
  T5AISleepFault_Controller,
  T5AISleepFault_Cleanup,
  T5AISleepFault_ResumeTimeout,
  T5AISleepFault_Deadline,
  T5AISleepFault_Owner,
  T5AISleepFault_NoControllerSleep,
  T5AISleepFault_AdmissionBudget,
  T5AISleepFault_WatchdogGuard,
} T5AISleepFault;

typedef struct {
  uint32_t requests, prepared, entries, aborted, dropped;
  uint32_t aon_ticks, cp_entries, cp_state, late_aon, fault;
  uint32_t rejected, cp_samples, missing_low, missing_high, cp_resources, cp_clock_cycles;
  uint32_t cp_mask_barriers;
} T5AISleepStats;

void t5ai_sleep_start(void);
void t5ai_sleep_service(uint32_t aon_deadline);
bool t5ai_sleep_enter(uint32_t aon_deadline);
void t5ai_sleep_stats(T5AISleepStats *stats);
