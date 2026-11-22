	/* A software re-pend can have queued RX data without hardware IRQ status. */
	if (int_status == 0 &&
	    !(mbox0_dev.chn_drv[SELF_CHNL]->chn_get_rx_fifo_stat(&mbox0_dev.hal) &
	      RX_FIFO_STAT_NOT_EMPTY)) {
		unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ISRReturn);
		return;
	}