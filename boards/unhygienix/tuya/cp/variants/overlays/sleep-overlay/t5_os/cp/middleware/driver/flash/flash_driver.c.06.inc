static void unhygienix_native_flash_prepare(void)
{
    s_native_flash_resume.valid = false;
    if (!unhygienix_native_sleep_prepared || !s_flash.flash_cfg)
        return;
    const uint32_t irq_state = flash_enter_critical();
    const flash_line_mode_t mode = s_flash.flash_cfg->line_mode;
    if (mode == FLASH_LINE_MODE_TWO || mode == FLASH_LINE_MODE_FOUR) {
        s_native_flash_resume.line_mode = mode;
        s_native_flash_resume.quad_bits = s_flash.flash_cfg->coutinuous_read_mode_bits_val;
        s_native_flash_resume.probe_address = &s_flash.flash_cfg->flash_id;
        s_native_flash_resume.probe_value = *s_native_flash_resume.probe_address;
        if (mode == FLASH_LINE_MODE_FOUR && !unhygienix_native_sleep_mask_only())
            flash_set_qe();
        s_native_flash_resume.valid = true;
    }
    flash_exit_critical(irq_state);
}

void unhygienix_native_flash_prepare_mask_only(void)
{
    if (unhygienix_native_sleep_mask_only())
        unhygienix_native_flash_prepare();
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_flash_ready(void)
{
    return unhygienix_native_sleep_prepared && s_native_flash_resume.valid;
}

__attribute__((section(".itcm_sec_code"))) bool unhygienix_native_flash_resume(void)
{
    if (!unhygienix_native_sleep_prepared || !s_native_flash_resume.valid ||
        !unhygienix_native_flash_irqs_locked())
        return false;
    if (unhygienix_native_sleep_mask_only()) {
        unhygienix_native_flash_barrier();
        if (*s_native_flash_resume.probe_address != s_native_flash_resume.probe_value)
            return false;
        s_native_flash_resume.valid = false;
        return true;
    }
    // AP is suspended; the CP lease owns flash and keeps IRQs masked throughout.
    flash_hal_clear_qwfr(&s_flash.hal);
#if CONFIG_SOC_BK7236XX
    sys_drv_set_sys2flsh_2wire(0);
#endif
    if (s_native_flash_resume.line_mode == FLASH_LINE_MODE_FOUR) {
        flash_hal_set_quad_m_value(&s_flash.hal, s_native_flash_resume.quad_bits);
        flash_hal_set_mode(&s_flash.hal, FLASH_MODE_QUAD);
    } else {
        flash_hal_set_mode(&s_flash.hal, FLASH_MODE_DUAL);
    }
    s_flash.flash_line_mode = s_native_flash_resume.line_mode;
#if CONFIG_SOC_BK7236XX
    sys_drv_set_sys2flsh_2wire(1);
#endif
    unhygienix_native_flash_barrier();
    if (*s_native_flash_resume.probe_address != s_native_flash_resume.probe_value)
        return false;
    s_native_flash_resume.valid = false;
    return true;
}

flash_line_mode_t bk_flash_get_line_mode(void)