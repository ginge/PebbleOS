/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! @file dbgserial_input.h
//! Contains the input related functionality of the debug serial port.

//! Initializes the input portions of the dbgserial driver.
void dbgserial_input_init(void);

typedef void (*DbgSerialCharacterCallback)(char c, bool* should_context_switch);
void dbgserial_register_character_callback(DbgSerialCharacterCallback callback);

//! Enables/disables DMA-based receiving
void dbgserial_set_rx_dma_enabled(bool enabled);

void dbgserial_set_input_enabled(bool enabled);

#ifdef CONFIG_SOC_T5AI
//! Consume a completed CP-to-AP mailbox probe. This deliberately does not expose the serial
//! console on T5AI.
bool dbgserial_input_consume_mailbox_probe(void);
#endif
