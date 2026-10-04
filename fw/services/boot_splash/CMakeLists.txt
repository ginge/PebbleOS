# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
if(CONFIG_SOC_T5AI)
  pbl_library_sources(service_t5ai.c)
else()
  pbl_library_sources(service.c)
endif()
