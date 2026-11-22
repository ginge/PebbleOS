#include "sys_pm_hal_ctrl.h"
extern volatile bool unhygienix_native_sleep_prepared;
extern volatile uint32_t unhygienix_native_lowvol_count;
extern bool unhygienix_native_sleep_subcores_ready(void);
#include <driver/unhygienix_sleep_protocol.h>
extern void unhygienix_native_sleep_stage(uint32_t stage);
extern uint32_t unhygienix_native_sleep_halt_config(uint32_t value);
extern bool unhygienix_native_sleep_hal_ready(uint64_t, uint32_t, uint32_t, uint32_t);
extern bk_err_t unhygienix_native_gpio_wake(void);
extern void unhygienix_native_sleep_wait(void);
extern bool unhygienix_native_sleep_keep_hf(void);
extern bool unhygienix_native_sleep_core_only(void);
extern void unhygienix_native_sleep_mask_enter(void);
extern void unhygienix_native_sleep_mask_exit(void);
extern bool unhygienix_native_sleep_flash_restore(void);
extern bk_err_t bk_flash_power_saving_exit(void);
extern uint32_t unhygienix_native_sleep_power_domains(uint32_t value);
extern bool unhygienix_native_sleep_bsp_restore(void);
extern void pm_low_voltage_bsp_restore(void);
extern uint32_t unhygienix_native_sleep_condition(uint32_t value);
extern bool unhygienix_native_sleep_domain_bypass(void);
extern bool unhygienix_native_sleep_retain_modules(void);
extern bool unhygienix_native_sleep_mask_required(void);
extern bool unhygienix_native_sleep_cp_gpio_owned(void);
extern bool unhygienix_native_normal_sleep_allowed(void);
extern bool unhygienix_native_sleep_mask_only(void);