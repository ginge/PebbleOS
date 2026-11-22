	sys_hal_backup_set_flash_26m(&cksel_flash, &clkdiv_flash);
	unhygienix_native_sleep_stage(T5AISleepStage_CP_CoreLow);

#if CONFIG_INT_WDT