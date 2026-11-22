	int_status = mbox0_dev.chn_drv[SELF_CHNL]->chn_get_int_status(&mbox0_dev.hal);
	extern void unhygienix_native_sleep_mailbox_irq_observed(uint32_t status);
	unhygienix_native_sleep_mailbox_irq_observed(int_status);