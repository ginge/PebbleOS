		unhygienix_native_mailbox_receive_observe(T5AIReceive_CompletionEnter, receive_header, 0u);
		log_chnl_cb_x[log_chnl_idx].tx_cmpl_isr(log_chnl_cb_x[log_chnl_idx].isr_param, (mb_chnl_ack_t *)ack_ptr);
		unhygienix_native_mailbox_receive_observe(T5AIReceive_CompletionReturned, receive_header, 0u);