        taskEXIT_CRITICAL();
        unhygienix_native_sleep_port_progress(T5AIPort_CriticalExited);
	}
	/* Tick accounting and the outer interrupt lock are restored before AP release. */
	extern void unhygienix_native_sleep_interrupts_restored(void);
	unhygienix_native_sleep_port_progress(T5AIPort_APReleaseEnter);
	unhygienix_native_sleep_interrupts_restored();
	unhygienix_native_sleep_port_progress(T5AIPort_Returned);
}
#endif