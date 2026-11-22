#include <driver/unhygienix_sleep_protocol.h>
#define NATIVE_MAILBOX_LOGICAL_STATE_SHIFT 8u
#define NATIVE_MAILBOX_CHANNEL_SHIFT 16u
bool unhygienix_native_mailbox_sleep_ready(void)
{
    const unsigned peer = GET_DST_CPU_ID(MB_CHNL_PWC);
    if (!mb_chnnl_init_ok || phy_chnl_x_cb[peer].tx_state != CHNL_STATE_IDLE)
        return false;
    const mb_log_chnl_cb_t *channels = phy_chnl_log_chnl_list[peer];
    for (unsigned logical = 0; logical < phy_chnl_log_chnl_num[peer]; ++logical) {
        if (channels[logical].tx_state != CHNL_STATE_IDLE)
            return false;
    }
    return true;
}

uint32_t unhygienix_native_mailbox_snapshot(T5AISleepSnapshot key)
{
    const unsigned peer = GET_DST_CPU_ID(MB_CHNL_PWC);
    const unsigned logical = GET_LOG_CHNL_ID(MB_CHNL_PWC);
    const mb_log_chnl_cb_t *channels = phy_chnl_log_chnl_list[peer];
    if (key == T5AISleepSnapshot_MailboxTXCount) return s_mailbox_tx_cnt;
    if (key == T5AISleepSnapshot_MailboxACKCount) return s_mailbox_tx_ack_cnt;
    return phy_chnl_x_cb[peer].tx_state |
        ((uint32_t)channels[logical].tx_state << NATIVE_MAILBOX_LOGICAL_STATE_SHIFT) |
        ((uint32_t)phy_chnl_x_cb[peer].tx_log_chnl << NATIVE_MAILBOX_CHANNEL_SHIFT);
}

static inline bk_err_t bk_mailbox_send_safe(mailbox_data_t *data, mailbox_endpoint_t src, mailbox_endpoint_t dst, void *arg)