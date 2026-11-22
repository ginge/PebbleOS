if(CONFIG_RECOVERY_FW OR VARIANT STREQUAL "prf")
  message(FATAL_ERROR "T5AI recovery firmware is disabled: no validated recovery flow")
endif()
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(init.c idle.c tick.c delay.c power.c retained.c reboot_reason.c flash_translation.c)
pbl_library_sources(resource_storage.c)
# ARM syscall exit calls this native hook from inline assembly.
pbl_link_options(-Wl,--undefined=pbl_kernel_syscall_exit)
pbl_library_sources_ifdef(CONFIG_T5AI_RETAINED_SLEEP sleep.c)
if(CONFIG_T5AI_RETAINED_SLEEP OR CONFIG_T5AI_SLEEP_GUARD_TEST)
  pbl_library_sources(sleep_blockers.c)
endif()
pbl_library_sources_ifdef(CONFIG_T5AI_NATIVE_ALARM native_alarm.c)
pbl_linker_sources(footer linker.ld)
pbl_linker_sources(memory psram-memory.ld)
pbl_linker_sources(ram-sections retained.ld framebuffer.ld)
pbl_include_directories(.)
pbl_library_sources(${PBL_BASE}/boards/unhygienix/unhygienix_standby.c)
include(${PBL_BASE}/boards/unhygienix/firmware.cmake)

# Assembly entry points must remain visible through LTO.
pbl_link_options(-Wl,--undefined=kernel_prep_c -Wl,--undefined=sched_switch_in)
pbl_linker_sources(ramfunc ramfunc.ld)

# Size optimization keeps upstream growth inside the fixed AP partition.
if(CONFIG_SIZE_OPTIMIZATIONS)
  pbl_compile_options(-Oz)
endif()

# Keep diagnostic compile/link changes separate from production integration.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/production.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/diagnostics.cmake)
