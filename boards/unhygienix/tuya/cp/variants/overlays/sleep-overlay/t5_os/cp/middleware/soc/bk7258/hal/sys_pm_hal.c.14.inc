	sys_hal_restore_int(int_state1, int_state2);
	unhygienix_native_sleep_mask_exit();
	unhygienix_native_sleep_stage(T5AISleepStage_CP_MaskRestored);

	portNVIC_SYSTICK_LOAD_REG