#include <driver/unhygienix_sleep_protocol.h>
#include "sys_ll.h"
extern bool unhygienix_native_psram_app_owned(void);
bool unhygienix_native_peripherals_retained(void)
{
    const uint32_t domain_base = PM_POWER_MODULE_NAME_BAKP * PM_MODULE_SUB_POWER_DOMAIN_MAX;
    const uint32_t required = (1u << (PM_POWER_SUB_MODULE_NAME_BAKP_SDIO % domain_base)) |
        (1u << (PM_POWER_SUB_MODULE_NAME_BAKP_DMA0 % domain_base));
    return (s_pm_bakp_pm_state & required) == required &&
        !sys_ll_get_cpu_power_sleep_wakeup_pwd_bakp();
}

bool unhygienix_native_memory_retained(void)
{
    const uint32_t psram_vote = 1u << (PM_POWER_SUB_MODULE_NAME_AHBP_PSRAM %
        (PM_POWER_MODULE_NAME_AHBP * PM_MODULE_SUB_POWER_DOMAIN_MAX));
    return unhygienix_native_psram_app_owned() && (s_pm_ahpb_pm_state & psram_vote) &&
        sys_ll_get_ana_reg13_enpsram();
}

static bk_err_t pm_psram_malloc_state_and_power_ctrl()