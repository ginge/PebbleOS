/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/mailbox_protocol.h>
#include <pbl/soc/t5ai/mipc.h>

#include <stddef.h>
#include <stdint.h>

// Pinned MIPC uses reflected CRC-8 polynomial 0xAB (normal form 0xD5).
#define MIPC_CRC_REFLECTED_POLYNOMIAL 0xabu
#define MIPC_CRC_BITS 8u
#define MIPC_SOURCE_MASK 0x7u
#define MIPC_TYPE_MASK 0x1fu
#define MIPC_TYPE_SHIFT 3u
#define MIPC_STATE_MASK 0xfu
#define MIPC_STATE_SHIFT 8u
#define MIPC_CONTROL_MASK 0xfu
#define MIPC_CONTROL_SHIFT 12u

uint8_t t5ai_mipc_crc8(const void *data, size_t length, uint8_t initial) {
  const uint8_t *bytes = data;
  uint8_t crc = initial;
  while (length-- != 0) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < MIPC_CRC_BITS; ++bit) {
      crc =
          (crc & 1u) ? (uint8_t)((crc >> 1) ^ MIPC_CRC_REFLECTED_POLYNOMIAL) : (uint8_t)(crc >> 1);
    }
  }
  return crc;
}

uint8_t t5ai_mipc_data_crc(const T5AIMIPCData *data) {
  if (data == NULL) {
    return 0;
  }
  return t5ai_mipc_crc8(&data->result, sizeof(*data) - sizeof(data->list) - sizeof(data->result),
                        T5AI_MIPC_CRC_INITIAL);
}

uint8_t t5ai_mipc_header(uint8_t source, uint8_t type) {
  return (source & MIPC_SOURCE_MASK) | (uint8_t)((type & MIPC_TYPE_MASK) << MIPC_TYPE_SHIFT);
}

uint32_t t5ai_mipc_frame_header(uint8_t command, uint8_t state, uint8_t control, uint8_t sequence,
                                uint8_t logical_channel) {
  return command | ((uint32_t)(state & MIPC_STATE_MASK) << MIPC_STATE_SHIFT) |
         ((uint32_t)(control & MIPC_CONTROL_MASK) << MIPC_CONTROL_SHIFT) |
         ((uint32_t)sequence << T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT) |
         ((uint32_t)logical_channel << T5AI_MAILBOX_HEADER_CHANNEL_SHIFT);
}

bool t5ai_mipc_frame_valid(const T5AIMIPCFrame *frame, const T5AIMIPCData *data) {
  if (frame == NULL || data == NULL || frame->object == 0) {
    return false;
  }
  /* The mailbox-channel driver removes its physical channel, sequence, and
   * control fields before delivering the logical 16-byte payload. Channel
   * validation therefore belongs at the route callback, not in this MIPC
   * payload helper. */
  return (uint8_t)frame->result == t5ai_mipc_data_crc(data);
}
