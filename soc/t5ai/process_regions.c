/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/kernel/thread.h>

#include <process_management/process_regions.h>
extern const uint8_t __APP_RAM__[], __APP_RAM_size__[];
extern const uint8_t __WORKER_RAM__[], __WORKER_RAM_size__[];
// STAR-MC1 caches normal memory only when it is non-shareable.
static const struct pbl_mpu_region s_app = {
  .name = "app",
  .base = (uintptr_t)__APP_RAM__,
  .size = (size_t)__APP_RAM_size__,
  .attr = PBL_MPU_P_RW | PBL_MPU_NORMAL_WB | PBL_MPU_EXEC,
};
static const struct pbl_mpu_region s_worker = {
  .name = "worker",
  .base = (uintptr_t)__WORKER_RAM__,
  .size = (size_t)__WORKER_RAM_size__,
  .attr = PBL_MPU_P_RW | PBL_MPU_NORMAL_WB | PBL_MPU_EXEC,
};
const struct pbl_mpu_region *process_region_app(void) {
  return &s_app;
}
const struct pbl_mpu_region *process_region_worker(void) {
  return &s_worker;
}
void process_regions_thread_init(void) {
  const struct pbl_mpu_region *regions[PBL_THREAD_MAX_MEM_REGIONS] = {
    [PROCESS_REGIONS_THREAD_APP] = &s_app,
    [PROCESS_REGIONS_THREAD_WORKER] = &s_worker,
  };
  pbl_thread_regions_default_set(regions);
}
