static void mb_phy_chnl_rx_ack_isr(mb_phy_chnl_ack_t *ack_ptr)
{
	const uint32_t receive_header = ack_ptr->hdr.data;
	unhygienix_native_mailbox_receive_observe(T5AIReceive_ACKEnter, receive_header, 0u);