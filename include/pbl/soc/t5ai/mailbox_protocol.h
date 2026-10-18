/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Logical channel: service index in bits 3:0, CPU direction in bits 7:4.
#define T5AI_MAILBOX_CHANNEL_BASE_MASK 0xf0u
#define T5AI_MAILBOX_CHANNEL_INDEX_MASK 0x0fu
// Wire header: command[7:0], status[11:8], control[15:12], sequence[23:16], channel[31:24].
#define T5AI_MAILBOX_HEADER_SEQUENCE_SHIFT 16u
#define T5AI_MAILBOX_HEADER_CHANNEL_SHIFT 24u
#define T5AI_MAILBOX_HEADER_COMMUNICATION_FAILED (1u << 8)

#define T5AI_MAILBOX_FRAME_SIZE 16u
#define T5AI_MAILBOX_TX_CHANNEL_BASE 0x10u
#define T5AI_MAILBOX_RX_CHANNEL_BASE 0x40u
#define T5AI_MAILBOX_CHANNEL_COUNT 16u
