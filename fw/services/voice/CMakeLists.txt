# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources
  transcription.c
  voice.c
  voice_speex.c
)
pbl_library()
pbl_library_sources(${sources})
pbl_library_sources_ifdef(CONFIG_SHELL shell.c)

if(CONFIG_SOC_T5AI)
  set_source_files_properties(voice_speex.c PROPERTIES COMPILE_DEFINITIONS
    "kernel_malloc_check=t5ai_voice_malloc_check;kernel_free=t5ai_voice_free")
endif()
