# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(excl)
if(CONFIG_RECOVERY_FW)
  list(APPEND excl
    resource_storage_file.c
  )
endif()
if(CONFIG_SOC_T5AI)
  list(APPEND excl resource_storage_flash.c)
endif()
pbl_library()
file(GLOB sources CONFIGURE_DEPENDS *.c)
pbl_filter_sources(sources EXCLUDE ${excl})
pbl_library_sources(${sources})
