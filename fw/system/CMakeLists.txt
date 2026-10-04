# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
file(GLOB sources CONFIGURE_DEPENDS *.c)
if(CONFIG_SOC_T5AI)
  pbl_filter_sources(sources EXCLUDE bootbits.c reboot_reason.c)
endif()
pbl_library_sources(${sources})
