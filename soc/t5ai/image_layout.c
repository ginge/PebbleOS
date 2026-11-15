/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include <pbl/kernel/compiler.h>
#include <pbl/soc/t5ai/image_layout.h>

#define T5AI_IMAGE_LAYOUT_VERSION 2u

_Static_assert(CONFIG_FW_FLASH_SIZE ==
                   (T5AI_FLASH_XIP_BASE + PART_FIRMWARE_SLOT_0_OFFSET + PART_FIRMWARE_SLOT_0_SIZE) -
                       (T5AI_FLASH_XIP_BASE + PART_FIRMWARE_SLOT_0_OFFSET),
               "Firmware linker and flash layout must agree");
_Static_assert((T5AI_FLASH_XIP_BASE + PART_SYSTEM_RESOURCES_BANK_0_OFFSET) ==
                   (T5AI_FLASH_XIP_BASE + PART_FIRMWARE_SLOT_0_OFFSET + PART_FIRMWARE_SLOT_0_SIZE),
               "Resources must follow firmware");
_Static_assert((T5AI_FLASH_XIP_BASE + PART_SYSTEM_RESOURCES_BANK_0_OFFSET +
                PART_SYSTEM_RESOURCES_BANK_0_SIZE) <= (T5AI_FLASH_XIP_BASE + PART_CD_OFFSET),
               "Resources must not overlap reserved crash data");

// Retained in ROM so host tools can reject mismatched firmware/resource layouts.
const struct {
  char magic[8];
  uint32_t version, firmware_base, firmware_size, resource_offset, resource_size;
} t5ai_image_layout PBL_USED = {
  .magic = "PBLT5LY2",
  .version = T5AI_IMAGE_LAYOUT_VERSION,
  .firmware_base = (T5AI_FLASH_XIP_BASE + PART_FIRMWARE_SLOT_0_OFFSET),
  .firmware_size = CONFIG_FW_FLASH_SIZE,
  .resource_offset = (T5AI_FLASH_XIP_BASE + PART_SYSTEM_RESOURCES_BANK_0_OFFSET) -
                     (T5AI_FLASH_XIP_BASE + PART_FIRMWARE_SLOT_0_OFFSET),
  .resource_size = (T5AI_FLASH_XIP_BASE + PART_SYSTEM_RESOURCES_BANK_0_OFFSET +
                    PART_SYSTEM_RESOURCES_BANK_0_SIZE) -
                   (T5AI_FLASH_XIP_BASE + PART_SYSTEM_RESOURCES_BANK_0_OFFSET),
};
