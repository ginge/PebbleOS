extern void unhygienix_native_sleep_reply_ack(uint32_t command, uint32_t generation,
                                              uint32_t abi, uint32_t communication_state);
static void pm_cp0_mailbox_tx_cmpl_isr(int *pm_mb, mb_chnl_ack_t *cmd_buf)
{
    (void)pm_mb;
    unhygienix_native_sleep_reply_ack(cmd_buf->hdr.cmd, cmd_buf->ack_data2,
                                       cmd_buf->ack_data3, cmd_buf->hdr.state);
}