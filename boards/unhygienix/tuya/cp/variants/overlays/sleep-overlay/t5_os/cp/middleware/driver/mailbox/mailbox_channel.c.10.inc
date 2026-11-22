		unhygienix_native_mailbox_receive_observe(T5AIReceive_CommandEnter, rx_data.phy_ch_hdr.data, 0u);
		mb_phy_chnl_rx_cmd_isr(&rx_data.phy_ch_cmd);
		unhygienix_native_mailbox_receive_observe(T5AIReceive_CommandReturned, 0u, 0u);