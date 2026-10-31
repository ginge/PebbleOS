/* SPDX-License-Identifier: Apache-2.0 */
#include "tkl_ipc.h"

#include <stddef.h>
#include <stdint.h>

#include <driver/mailbox_channel.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>

const uint32_t unhygienix_cp_contract[] = {
  0x55435031u,
  sizeof(mb_chnl_cmd_t),
  sizeof(struct ipc_msg_s),
  offsetof(struct ipc_msg_s, req_param),
  offsetof(struct ipc_msg_s, res_param),
  TKL_IPC_TYPE_HCI,
  TKL_IPC_TYPE_SYS,
  TKL_IPC_TYPE_HCI_RESET,
  PM_CLK_ID_UART1,
  PM_CLK_ID_QSPI_1,
  PM_CLK_ID_SDIO,
  PM_CLK_ID_AUDIO,
  PM_POWER_SUB_MODULE_NAME_BAKP_SDIO,
  PM_POWER_SUB_MODULE_NAME_BAKP_DMA0,
  PM_POWER_SUB_MODULE_NAME_AHBP_QSPI,
  PM_POWER_SUB_MODULE_NAME_AUDP_AUDIO,
  PM_POWER_PSRAM_MODULE_NAME_APP,
  PM_SLEEP_MODULE_NAME_APP,
  PM_DEV_ID_CPU1,
  PM_CPU_FRQ_240M,
  PM_CPU_FRQ_320M,
  PM_CPU_FRQ_480M,
  MB_CHNL_HW_CTRL,
  MB_CHNL_PWC,
  MB_CHNL_LOG,
  MB_CHNL_FLASH,
};
