		unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_LogicalCallback);
		log_chnl_cb_x[log_chnl_idx].rx_isr(log_chnl_cb_x[log_chnl_idx].isr_param, (mb_chnl_cmd_t *)cmd_ptr);
		unhygienix_native_sleep_mailbox_progress(T5AIMailboxProgress_LogicalCallbackReturned);