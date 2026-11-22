extern void unhygienix_native_sleep_stage(uint32_t stage);
extern bk_err_t unhygienix_native_gpio_enter(void *);
extern bk_err_t unhygienix_native_gpio_exit(void *);
#include <driver/unhygienix_sleep_protocol.h>

uint64_t unhygienix_native_sleep_missing_votes(void)
{
    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    const uint64_t missing = s_pm_enter_low_vol_modules & ~s_pm_sleeped_modules;
    GLOBAL_INT_RESTORE();
    return missing;
}

void pm_debug_ctrl(uint32_t debug_en)