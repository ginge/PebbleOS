if(CONFIG_RECOVERY_FW OR VARIANT STREQUAL "prf")
  message(FATAL_ERROR "T5AI recovery firmware is disabled: no validated recovery flow")
endif()
# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(init.c idle.c delay.c power.c retained.c bootbits.c reboot_reason.c flash_translation.c)
pbl_library_sources_ifdef(CONFIG_T5AI_STATE_TRACE state_trace.c)
pbl_library_sources_ifdef(CONFIG_SERVICE_VOICE voice_memory.c)
pbl_linker_sources(footer linker.ld)
pbl_linker_sources(memory psram-memory.ld)
pbl_linker_sources(ram-sections retained.ld framebuffer.ld)
pbl_include_directories(.)
include(${PBL_BASE}/boards/unhygienix/firmware.cmake)

# Assembly entry points must remain visible through LTO.
pbl_link_options(-Wl,--undefined=kernel_prep_c -Wl,--undefined=sched_switch_in)
pbl_linker_sources(ramfunc ramfunc.ld)

# Size optimization keeps upstream growth inside the fixed AP partition.
if(CONFIG_SIZE_OPTIMIZATIONS)
  pbl_compile_options(-Oz)
endif()
