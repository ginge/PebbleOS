# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
file(GLOB sources CONFIGURE_DEPENDS *.c)
if(CONFIG_SOC_T5AI)
  pbl_filter_sources(sources EXCLUDE bootbits.c reboot_reason.c)
endif()
# The profiler counts Cortex-M cycles.
if(NOT CONFIG_CPU_CORTEX_M)
  pbl_filter_sources(sources EXCLUDE profiler.c)
endif()
pbl_library_sources(${sources})
