// Three queued pointer payloads plus the AP's popped-but-not-copied frame.
#define NATIVE_MAILBOX_MAX_RX_FIFO_DEPTH 3u
#define NATIVE_MAILBOX_SNAPSHOT_SLOTS 1u
#define NATIVE_MAILBOX_TX_SLOT_COUNT \
    (NATIVE_MAILBOX_MAX_RX_FIFO_DEPTH + NATIVE_MAILBOX_SNAPSHOT_SLOTS)
static mailbox_data_t mailbox_buff[MBOX_CHNL_NUM][NATIVE_MAILBOX_TX_SLOT_COUNT];