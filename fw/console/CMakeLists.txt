# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(excl)
if(NOT CONFIG_SHELL)
  list(APPEND excl
    shell_dbgserial.c
    shell_pulse.c
  )
endif()
pbl_library()
file(GLOB sources CONFIGURE_DEPENDS *.c)
if(CONFIG_SOC_T5AI)
  list(APPEND excl dbgserial.c dbgserial_input.c pulse_flash_imaging.c pulse_bulkio_external_flash.c pulse_bulkio_memory.c)
else()
  list(APPEND excl dbgserial_t5ai.c dbgserial_input_t5ai.c)
endif()
pbl_filter_sources(sources EXCLUDE ${excl})
pbl_library_sources(${sources})
