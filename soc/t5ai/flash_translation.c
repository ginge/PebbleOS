/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/soc/t5ai/flash_translation.h>

#include <limits.h>
#include <stddef.h>

bool t5ai_flash_logical_to_physical(uint32_t logical, uint32_t *physical) {
  if (physical == NULL) {
    return false;
  }

  const uint32_t units = logical / T5AI_FLASH_LOGICAL_UNIT_SIZE;
  const uint32_t remainder = logical % T5AI_FLASH_LOGICAL_UNIT_SIZE;
  if (units > (UINT32_MAX - remainder) / T5AI_FLASH_PHYSICAL_UNIT_SIZE) {
    return false;
  }

  *physical = units * T5AI_FLASH_PHYSICAL_UNIT_SIZE + remainder;
  return true;
}

bool t5ai_flash_physical_address_is_data(uint32_t physical) {
  return physical % T5AI_FLASH_PHYSICAL_UNIT_SIZE <
         T5AI_FLASH_LOGICAL_UNIT_SIZE;
}

bool t5ai_flash_physical_to_logical(uint32_t physical, uint32_t *logical) {
  if (logical == NULL || !t5ai_flash_physical_address_is_data(physical)) {
    return false;
  }

  const uint32_t units = physical / T5AI_FLASH_PHYSICAL_UNIT_SIZE;
  const uint32_t remainder = physical % T5AI_FLASH_PHYSICAL_UNIT_SIZE;
  *logical = units * T5AI_FLASH_LOGICAL_UNIT_SIZE + remainder;
  return true;
}
