# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(button.c debounced_button.c mcu.c mailbox_channel.c cp_mailbox.c mb_ipc.c saradc_ipc.c mipc.c put_bytes_job_buffer.c)
pbl_library_sources_ifdef(CONFIG_T5AI_SD_PFS sd.c)
pbl_library_sources(../otp/otp_none.c)
if(NOT CONFIG_TEMPERATURE)
  pbl_library_sources(../temperature/temperature.c ../temperature/temperature_none.c)
endif()
if(NOT CONFIG_VIBE)
  pbl_library_sources(../vibe/vibe.c ../vibe/vibe_none.c)
endif()

# Stock services call the ALS class even on boards without a physical sensor.
if(NOT CONFIG_AMBIENT_LIGHT)
  pbl_library_sources(../ambient/als.c ../ambient/ambient_light_none.c)
  pbl_library_compile_definitions(CONFIG_DRIVER_AMBIENT_LOG_LEVEL=0)
endif()
