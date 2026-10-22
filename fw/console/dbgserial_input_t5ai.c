/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include "dbgserial_input.h"

void dbgserial_input_init(void) {
}

void dbgserial_register_character_callback(DbgSerialCharacterCallback callback) {
  (void)callback;
}

void dbgserial_set_rx_dma_enabled(bool enabled) {
  (void)enabled;
}

void dbgserial_set_input_enabled(bool enabled) {
  (void)enabled;
}
