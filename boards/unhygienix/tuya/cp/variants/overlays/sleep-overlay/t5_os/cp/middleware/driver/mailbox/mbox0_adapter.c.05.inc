		unhygienix_native_mailbox_receive_observe(T5AIReceive_CopyEnter, 0u, 0u);
		memcpy(&data, (u8 *)msg->data[0], sizeof(data));
		unhygienix_native_mailbox_receive_observe(T5AIReceive_CopyReturned, data.param0, 0u);