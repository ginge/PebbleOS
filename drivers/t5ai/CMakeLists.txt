# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(mcu.c mailbox_channel.c cp_mailbox.c mb_ipc.c saradc_ipc.c mipc.c put_bytes_job_buffer.c)
pbl_library_sources_ifdef(CONFIG_T5AI_SD_PFS sd.c)
pbl_library_sources(../otp/otp_none.c)
