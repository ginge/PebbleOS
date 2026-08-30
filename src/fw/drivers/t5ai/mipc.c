/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/mipc.h>

#include <stddef.h>
#include <stdint.h>

uint8_t t5ai_mipc_crc8(const void *data, size_t length, uint8_t initial) {
  const uint8_t *bytes = data;
  uint8_t crc = initial;
  while (length-- != 0) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0xf7u)
                          : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

uint8_t t5ai_mipc_data_crc(const T5AIMIPCData *data) {
  if (data == NULL) {
    return 0;
  }
  return t5ai_mipc_crc8(&data->result,
                        sizeof(*data) - sizeof(data->list) -
                            sizeof(data->result),
                        T5AI_MIPC_CRC_INITIAL);
}

uint8_t t5ai_mipc_header(uint8_t source, uint8_t type) {
  return (source & 0x7u) | (uint8_t)((type & 0x1fu) << 3);
}

uint32_t t5ai_mipc_frame_header(uint8_t command, uint8_t state,
                                uint8_t control, uint8_t sequence,
                                uint8_t logical_channel) {
  return command | ((uint32_t)(state & 0xfu) << 8) |
         ((uint32_t)(control & 0xfu) << 12) |
         ((uint32_t)sequence << 16) |
         ((uint32_t)logical_channel << 24);
}

bool t5ai_mipc_frame_valid(const T5AIMIPCFrame *frame,
                           const T5AIMIPCData *data) {
  if (frame == NULL || data == NULL || frame->object == 0) {
    return false;
  }
  if ((uint8_t)(frame->header >> 24) != T5AI_MIPC_LOGICAL_CHANNEL) {
    return false;
  }
  return (uint8_t)frame->result == t5ai_mipc_data_crc(data);
}
