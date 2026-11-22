
	/* Native AP receives its resume event after resource restoration. */
	if (!unhygienix_native_sleep_prepared) bk_pm_cp_wakeup_ap_from_wfi(0);
#endif