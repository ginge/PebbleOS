	sys_hal_restore_core_freq(cksel_core, clkdiv_core, clkdiv_bus);
	unhygienix_native_sleep_stage(T5AISleepStage_CP_CoreRestored);
	sys_hal_restore_flash_freq(cksel_flash, clkdiv_flash);
	unhygienix_native_sleep_stage(T5AISleepStage_CP_FlashRestored);
	sys_hal_restore_int(int_state1, int_state2);
	unhygienix_native_sleep_stage(T5AISleepStage_CP_IrqRestored);