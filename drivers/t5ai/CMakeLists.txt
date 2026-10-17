# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(button.c debounced_button.c mcu.c mailbox_channel.c cp_mailbox.c mb_ipc.c mipc.c put_bytes_job_buffer.c)
pbl_library_sources(../temperature/temperature_none.c ../vibe/vibe_none.c ../otp/otp_none.c)

# Stock services call the ALS class even on boards without a physical sensor.
if(NOT CONFIG_AMBIENT_LIGHT)
  pbl_library_sources(../ambient/als.c ../ambient/ambient_light_none.c)
  pbl_library_compile_definitions(CONFIG_DRIVER_AMBIENT_LOG_LEVEL=0)
endif()
