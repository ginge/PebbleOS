/* SPDX-License-Identifier: Apache-2.0 */
#include "flash_region/flash_region.h"

#include <errno.h>

#include <pbl/drivers/flash/flash_impl.h>

// The class uses offsets; the existing CP policy checks use the mapped aperture.
static int prv_class_init(const struct pbl_flash *flash) {
  return FAILED(flash_impl_init(pbl_flash_in_coredump(flash))) ? -EIO : 0;
}
static int prv_class_read(const struct pbl_flash *flash, uint32_t addr, void *buf, size_t len) {
  if (len == 0)
    return 0;
  return FAILED(flash_impl_read_sync(buf, FLASH_REGION_BASE_ADDRESS + addr, len)) ? -EIO : 0;
}
static int prv_class_write(const struct pbl_flash *flash, uint32_t addr, const void *buf,
                           size_t len) {
  const uint8_t *src = buf;
  while (len != 0) {
    int written = flash_impl_write_page_begin(src, FLASH_REGION_BASE_ADDRESS + addr, len);
    if (written <= 0)
      return -EIO;
    if (FAILED(flash_impl_get_write_status()))
      return -EIO;
    addr += (uint32_t)written;
    src += written;
    len -= (size_t)written;
  }
  return 0;
}
static int prv_class_erase(const struct pbl_flash *flash, uint32_t addr, uint32_t size) {
  status_t status = size == SECTOR_SIZE_BYTES
                        ? flash_impl_erase_sector_begin(FLASH_REGION_BASE_ADDRESS + addr)
                        : flash_impl_erase_subsector_begin(FLASH_REGION_BASE_ADDRESS + addr);
  if (FAILED(status))
    return -EIO;
  return FAILED(flash_impl_get_erase_status()) ? -EIO : 1;
}
static const struct pbl_flash_ops s_ops = {
  .init = prv_class_init,
  .read = prv_class_read,
  .write = prv_class_write,
  .erase = prv_class_erase,
};
static const struct pbl_flash_geometry s_geometry = {
  .size = BOARD_NOR_FLASH_SIZE,
  .page_size = PAGE_SIZE_BYTES,
  .sector_size = SECTOR_SIZE_BYTES,
  .subsector_size = SUBSECTOR_SIZE_BYTES,
  .sector_erase_ms = 2,
  .subsector_erase_ms = 2,
};
PBL_FLASH_STATE_DEFINE(T5AI_FLASH);
const struct pbl_flash T5AI_FLASH = PBL_FLASH_INIT(T5AI_FLASH, "t5ai-cp-flash", nullptr, nullptr,
                                                   &s_ops, &s_geometry, nullptr, nullptr);
PBL_DEVICE_REGISTER(T5AI_FLASH, &T5AI_FLASH.dev);
