	if (unhygienix_native_sleep_mask_only()) {
        unhygienix_native_sleep_wait();
        sys_hal_restore_int(int_state1, int_state2);
        unhygienix_native_sleep_mask_exit();
        portNVIC_SYSTICK_CTRL_REG = systick_ctrl_value;
        return;
    }
	if (unhygienix_native_sleep_core_only()) {
        sys_hal_backup_set_core_26m(&cksel_core, &clkdiv_core, &clkdiv_bus);
        sys_hal_backup_set_flash_26m(&cksel_flash, &clkdiv_flash);
        if (unhygienix_native_sleep_bsp_restore()) {
#if CONFIG_INT_WDT
            bk_wdt_stop();
#if CONFIG_TASK_WDT
            bk_task_wdt_stop();
#endif
#endif
        }
        unhygienix_native_sleep_wait();
        if (unhygienix_native_sleep_flash_restore()) bk_flash_power_saving_exit();
        if (unhygienix_native_sleep_bsp_restore()) pm_low_voltage_bsp_restore();
        sys_hal_restore_core_freq(cksel_core, clkdiv_core, clkdiv_bus);
        sys_hal_restore_flash_freq(cksel_flash, clkdiv_flash);
        sys_hal_restore_int(int_state1, int_state2);
        portNVIC_SYSTICK_CTRL_REG = systick_ctrl_value;
        return;
    }
	sys_hal_mask_cpu0_int();
#if CONFIG_GPIO_WAKEUP_SUPPORT