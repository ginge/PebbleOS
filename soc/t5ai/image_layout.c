/* SPDX-License-Identifier: Apache-2.0 */
#include "flash_region/flash_region.h"

#include <stdint.h>

#include <pbl/kernel/compiler.h>

#define T5AI_IMAGE_LAYOUT_VERSION 2u

_Static_assert(CONFIG_FW_FLASH_SIZE ==
                   FLASH_REGION_FIRMWARE_SLOT_0_END - FLASH_REGION_FIRMWARE_SLOT_0_BEGIN,
               "Firmware linker and flash layout must agree");
_Static_assert(FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN == FLASH_REGION_FIRMWARE_SLOT_0_END,
               "Resources must follow firmware");
_Static_assert(FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END <= FLASH_REGION_CD_BEGIN,
               "Resources must not overlap reserved crash data");

// Retained in ROM so host tools can reject mismatched firmware/resource layouts.
const struct {
  char magic[8];
  uint32_t version, firmware_base, firmware_size, resource_offset, resource_size;
} t5ai_image_layout PBL_USED = {
  .magic = "PBLT5LY2",
  .version = T5AI_IMAGE_LAYOUT_VERSION,
  .firmware_base = FLASH_REGION_FIRMWARE_SLOT_0_BEGIN,
  .firmware_size = CONFIG_FW_FLASH_SIZE,
  .resource_offset =
      FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN - FLASH_REGION_FIRMWARE_SLOT_0_BEGIN,
  .resource_size =
      FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END - FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN,
};
