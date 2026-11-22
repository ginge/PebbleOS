	extern void unhygienix_native_sleep_stage(uint32_t stage);
	extern void unhygienix_native_analog_wake_prepare(void);
	unhygienix_native_sleep_stage(T5AISleepStage_CP_Resources);
	unhygienix_native_analog_wake_prepare();
	pm_psram_malloc_state_and_power_ctrl();