if(CONFIG_RECOVERY_FW OR VARIANT STREQUAL "prf")
  message(FATAL_ERROR "T5AI recovery firmware is disabled: no validated recovery flow")
endif()
# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(init.c idle.c tick.c delay.c power.c retained.c bootbits.c reboot_reason.c flash_translation.c)
pbl_library_sources(resource_storage.c)
pbl_library_sources_ifdef(CONFIG_T5AI_STATE_TRACE state_trace.c)
pbl_library_sources_ifdef(CONFIG_T5AI_REBOOT_TRACE reboot_trace.c)
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

if(CONFIG_T5AI_REMINDER_PROFILE)
  pbl_library_sources(reminder_profile.c)
  foreach(symbol settings_file_rewrite_filtered pfs_open pfs_close flash_read_bytes
                 flash_write_bytes flash_erase_sector_blocking pbl_mutex_lock_lr
                 flash_impl_write_page_begin sys_pbl_analytics_add pbl_mutex_unlock
                 timeline_item_storage_flush)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  # Preserve call boundaries for the diagnostic linker wrappers only.
  function(t5ai_reminder_profile_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/kernel/mutex.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    foreach(pair
        "fw/services/blob_db|timeline_item_storage.c"
        "fw/services/settings|settings_file.c"
        "fw/services/settings|settings_raw_iter.c"
        "fw/services/filesystem|pfs.c"
        "fw/services/filesystem|flash_translation.c"
        "fw/drivers/flash|flash_api.c"
        "fw/drivers/flash|flash_erase.c")
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source}
        DIRECTORY ${PBL_BASE}/${directory} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_reminder_profile_call_boundaries)
endif()

if(CONFIG_T5AI_INTERRUPT_PROFILE)
  pbl_library_sources(interrupt_profile.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tick.c APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  foreach(symbol arch_irq_disable arch_irq_enable SysTick_Handler)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  # Keep calls and vector references visible to the diagnostic linker wrappers.
  function(t5ai_interrupt_profile_call_boundaries)
    foreach(source irq.c arch/arm/arch.c arch/arm/vector_table.c)
      set_property(SOURCE ${PBL_BASE}/kernel/${source}
        DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_interrupt_profile_call_boundaries)
endif()

if(CONFIG_T5AI_HEAP_TRACE)
  pbl_library_sources(heap_trace.c)
  foreach(symbol heap_malloc heap_zalloc heap_calloc heap_realloc heap_free croak_oom)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_heap_trace_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/lib/util/heap.c
      DIRECTORY ${PBL_BASE}/lib/util APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    foreach(source pbl_malloc.c kernel_heap.c)
      set_property(SOURCE ${PBL_BASE}/fw/kernel/${source}
        DIRECTORY ${PBL_BASE}/fw/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_heap_trace_call_boundaries)
endif()

if(CONFIG_T5AI_FAULT_TRACE)
  pbl_library_sources(fault_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/fault_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  function(t5ai_fault_trace_call_boundaries)
    # Vector weak aliases are defined locally and cannot be intercepted by --wrap.
    set_property(SOURCE ${PBL_BASE}/fw/hardfault_handler.c
      DIRECTORY ${PBL_BASE}/fw APPEND PROPERTY COMPILE_DEFINITIONS
      HardFault_Handler=t5ai_stock_HardFault_Handler)
    set_property(SOURCE ${PBL_BASE}/kernel/arch/arm/vector_table.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_fault_trace_call_boundaries)
endif()
