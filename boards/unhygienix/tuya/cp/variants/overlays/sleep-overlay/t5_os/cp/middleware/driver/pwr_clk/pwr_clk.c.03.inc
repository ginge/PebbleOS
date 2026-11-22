bool unhygienix_native_psram_app_owned(void)
{
#if CONFIG_PSRAM
    return (s_pm_psram_ctrl_state & (1u << PM_POWER_PSRAM_MODULE_NAME_APP)) != 0u;
#else
    return false;
#endif
}

uint32_t bk_pm_get_psram_ctrl_state()