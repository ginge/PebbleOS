	log_chnl = CPX_LOG_CHNL_START(SELF_CPU, phy_chnl_idx) + log_chnl_idx;

	unhygienix_native_mailbox_receive_observe(T5AIReceive_NextSendEnter, receive_header, 0u);
	ret_code = mb_phy_chnl_tx_cmd(log_chnl);
	unhygienix_native_mailbox_receive_observe(T5AIReceive_NextSendReturned, receive_header, 0u);