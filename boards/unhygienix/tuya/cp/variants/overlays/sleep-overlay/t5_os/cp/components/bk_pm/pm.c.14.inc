	rtos_exit_critical(int_level);
	/* Release AP only after CP interrupt service can accept its next request. */
	extern void unhygienix_native_sleep_resumed(void);
	unhygienix_native_sleep_resumed();

	if (s_debug_en & 0x2)