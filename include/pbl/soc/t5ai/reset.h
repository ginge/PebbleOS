/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/compiler.h>

// Explicit restore timeouts encode the sampled blockers; zero means a missing CP event.
#define T5AI_RESET_REASON_SLEEP_WAIT_BASE   0x40u
#define T5AI_RESET_REASON_SLEEP_WAIT_READY  (1u << 0)
#define T5AI_RESET_REASON_SLEEP_WAIT_CORE   (1u << 1)
#define T5AI_RESET_REASON_SLEEP_WAIT_MEMORY (1u << 2)
#define T5AI_RESET_REASON_SLEEP_WAIT_MASK   0x07u
// Retained-test PM timeout detail stays within the AP-owned sleep reason range.
#define T5AI_RESET_REASON_SLEEP_PM_CANCEL  0x48u
#define T5AI_RESET_REASON_SLEEP_PM_PREPARE 0x49u
#define T5AI_RESET_REASON_SLEEP_PM_STATE   0x4au
#define T5AI_RESET_REASON_SLEEP_PM_COUNT   0x4bu
#define T5AI_RESET_REASON_SLEEP_PM_OTHER   0x4cu
#define T5AI_RESET_REASON_SLEEP_PM_COMMAND 0x4du
#define T5AI_RESET_REASON_SLEEP_PM_PENDING 0x4eu
#define T5AI_RESET_REASON_SLEEP_RESUME     0x4fu

// Alternate committed receive-record diagnostic; frozen configuration selects this encoding.
#define T5AI_RESET_REASON_CP_RECEIVE_BASE T5AI_RESET_REASON_SLEEP_WAIT_BASE
// Alternate owner comparison, selected only by its frozen diagnostic configuration.
#define T5AI_RESET_REASON_CP_RECEIVE_INVALID  0x50u
#define T5AI_RESET_REASON_CP_RECEIVE_CHANNEL  (1u << 0)
#define T5AI_RESET_REASON_CP_RECEIVE_SEQUENCE (1u << 1)
#define T5AI_RESET_REASON_CP_RECEIVE_ACK      (1u << 2)
#define T5AI_RESET_REASON_CP_RECEIVE_QUEUED   (1u << 3)

// Alternative default-off PM FIFO diagnostic; frozen configuration selects this encoding.
#define T5AI_RESET_REASON_PM_FIFO_BASE        0x40u
#define T5AI_RESET_REASON_PM_CP_RX_PENDING    (1u << 0)
#define T5AI_RESET_REASON_PM_AP_RX_PENDING    (1u << 1)
#define T5AI_RESET_REASON_PM_AP_ROUTE_ENABLED (1u << 2)
#define T5AI_RESET_REASON_PM_AP_IRQ_MASKED    (1u << 3)

// Alternate CP-side snapshot, selected only by its frozen diagnostic configuration.
#define T5AI_RESET_REASON_PM_CP_ROUTE_ENABLED (1u << 0)
#define T5AI_RESET_REASON_PM_CP_IRQ_ENABLED   (1u << 1)
#define T5AI_RESET_REASON_PM_CP_IRQ_MASKED    (1u << 2)
#define T5AI_RESET_REASON_PM_CP_HALTED        (1u << 3)
// Alternate hash-bound CP SRAM trace diagnostic, selected by the frozen config.
#define T5AI_RESET_REASON_PM_CP_TRACE_VALID      (1u << 0)
#define T5AI_RESET_REASON_PM_CP_TRACE_GENERATION (1u << 1)
#define T5AI_RESET_REASON_PM_CP_TRACE_RESTORED   (1u << 2)
#define T5AI_RESET_REASON_PM_CP_TRACE_VETO       (1u << 3)
// Alternate current-generation CP restoration boundary encoding.
#define T5AI_RESET_REASON_PM_CP_BOUNDARY_STARTED  0x41u
#define T5AI_RESET_REASON_PM_CP_BOUNDARY_VETO     0x42u
#define T5AI_RESET_REASON_PM_CP_BOUNDARY_READY    0x43u
#define T5AI_RESET_REASON_PM_CP_BOUNDARY_SENT     0x44u
#define T5AI_RESET_REASON_PM_CP_BOUNDARY_RELEASED 0x45u
#define T5AI_RESET_REASON_PM_CP_BOUNDARY_OTHER    0x46u
// Alternate CP live-state encoding; these independent reads are diagnostic only.
#define T5AI_RESET_REASON_PM_CP_CANCELLED (1u << 0)
#define T5AI_RESET_REASON_PM_CP_PREPARED  (1u << 1)
#define T5AI_RESET_REASON_PM_CP_RESOURCES (1u << 2)
#define T5AI_RESET_REASON_PM_CP_CRITICAL  (1u << 3)
// Alternate critical-history encoding, selected by its frozen diagnostic config.
#define T5AI_RESET_REASON_PM_CP_NESTING_MAX    7u
#define T5AI_RESET_REASON_PM_CP_NESTING_STABLE (1u << 3)

// Alternate read-only CP runtime encoding, selected by the frozen config.
#define T5AI_RESET_REASON_PM_CP_EXCEPTION           (1u << 0)
#define T5AI_RESET_REASON_PM_CP_IDLE_TASK           (1u << 1)
#define T5AI_RESET_REASON_PM_CP_SCHEDULER_SUSPENDED (1u << 2)
#define T5AI_RESET_REASON_PM_CP_TICK_BACKLOG        (1u << 3)
#define T5AI_RESET_REASON_PM_CP_RUNTIME_RELEASED    (1u << 3)

static inline bool t5ai_reset_reason_is_sleep_timeout(uint8_t reason) {
  return reason >= T5AI_RESET_REASON_SLEEP_WAIT_BASE && reason <= T5AI_RESET_REASON_SLEEP_RESUME;
}
#define T5AI_RESET_REASON_MAILBOX_BASE 0x50u
#define T5AI_RESET_REASON_PEBBLE_BASE  0x60u

// Snapshot CP-saved boot causes once, before native cache setup.
void t5ai_reset_reason_capture_boot(void);
uint8_t t5ai_reset_reason_get_ap(void);
uint8_t t5ai_reset_reason_get_boot_ap(void);
uint8_t t5ai_reset_reason_get_boot_cp(void);
void t5ai_reset_reason_set_ap(uint8_t reason);

/* Uses both BK7258 watchdog domains and the AON reset mask so CPU0, CPU1,
 * radio, mailbox, and their retained transaction state restart together. */
[[noreturn]] void t5ai_force_whole_chip_reset(uint8_t reason);

// Final idle check only: CP ABI v5 must hold its AON-driver writes until cleanup.
// Keeps DIVD or external 32 kHz; rejects unproved selectors or halted LPO.
bool t5ai_watchdog_sleep_guard_arm(void);
void t5ai_watchdog_sleep_guard_restore(void);

// Emergency path: uses only SRAM instructions, AON and watchdog registers.
[[noreturn]] void t5ai_force_whole_chip_reset_from_sram(uint8_t reason);

#ifdef CONFIG_T5AI_SLEEP_GUARD_TEST
void t5ai_sleep_guard_test_start(void);
#endif
