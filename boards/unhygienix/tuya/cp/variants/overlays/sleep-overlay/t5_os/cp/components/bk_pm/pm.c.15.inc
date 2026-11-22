	if (unhygienix_native_sleep_mask_only()) {
		unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
		unhygienix_native_flash_prepare_mask_only();
	} else {
		pm_low_voltage_resource_set();
	}
