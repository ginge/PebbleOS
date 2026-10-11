/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

enum {
  T5IO_READ_LOCK,
  T5IO_WRITE_LOCK,
  T5IO_WRITE_SEND,
  T5IO_WRITE_WAIT,
  T5IO_WRITE_ACK,
  T5IO_WRITE_TOTAL,
  T5IO_WRITE_SOCKET_LOCK,
  T5IO_WRITE_PHYSICAL,
  T5IO_WRITE_LOGICAL,
  T5IO_COUNT
};
void t5ai_io_trace_sample(unsigned stage, uint32_t ticks);
void t5ai_io_trace_dump(void);

void t5ai_flash_phase_trace_begin(void);
void t5ai_flash_phase_trace_dump(void);
void t5ai_flash_phase_trace_end(void);
