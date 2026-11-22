	extern void unhygienix_native_sleep_mailbox_progress(uint32_t progress);
	unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_PMQueue);
	bk_err_t result = bk_low_pwr_core_send_msg(&msg);
	unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_PMQueued);
	return result;