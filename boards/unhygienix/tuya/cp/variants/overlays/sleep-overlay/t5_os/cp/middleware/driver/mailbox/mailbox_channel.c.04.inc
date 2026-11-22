	unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_ACKSend);
	ret_code = bk_mailbox_send_safe((mailbox_data_t *)cmd_ptr, SELF_CPU, dst_cpu, (void *)&chnl_type);	/* mb_phy_chnl_tx_ack. */
	unhygienix_native_sleep_mailbox_progress(ret_code == BK_OK ? T5AIMailboxProgress_ACKSent : T5AIMailboxProgress_ACKFailed);