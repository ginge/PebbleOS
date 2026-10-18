if(CONFIG_RECOVERY_FW OR VARIANT STREQUAL "prf")
  message(FATAL_ERROR "T5AI recovery firmware is disabled: no validated recovery flow")
endif()
# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(init.c idle.c tick.c delay.c power.c retained.c reboot_reason.c flash_translation.c)
pbl_library_sources(resource_storage.c)
pbl_library_sources_ifdef(CONFIG_T5AI_DISPLAY_PROFILE display_profile.c)
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
        "drivers/flash|flash_api.c"
        "drivers/flash|flash_erase.c")
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

if(CONFIG_T5AI_BG_PICK_TRACE)
  pbl_library_sources(bg_pick_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bg_pick_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  pbl_link_options(-Wl,--wrap=sched_switch_in -Wl,--wrap=system_task_timer_init)
  function(t5ai_bg_pick_trace_call_boundaries)
    foreach(source sched.c arch/arm/arch.c)
      set_property(SOURCE ${PBL_BASE}/kernel/${source} DIRECTORY ${PBL_BASE}/kernel
        APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    set_property(SOURCE ${PBL_BASE}/fw/services/system_task/service.c
      DIRECTORY ${PBL_BASE}/fw/services/system_task APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_bg_pick_trace_call_boundaries)
endif()

if(CONFIG_T5AI_WDT_RECOVERY_TRACE)
  if(CONFIG_T5AI_REMINDER_PROFILE)
    message(FATAL_ERROR "Select one settings linker-wrapper profiler at a time")
  endif()
  pbl_library_sources(wdt_recovery_trace.c settings_sync_trace.c flash_phase_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wdt_recovery_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/settings_sync_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_phase_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  pbl_link_options(-Wl,--wrap=pbl_task_wdt_add -Wl,--wrap=settings_blob_db_mark_all_dirty)
  foreach(symbol pfs_open pfs_close prefs_private_lock alerts_preferences_lock
                 pbl_mutex_lock_lr pbl_mutex_unlock flash_read_bytes flash_write_bytes
                 flash_erase_sector_blocking settings_file_mark_all_dirty
                 settings_raw_iter_write_header settings_raw_iter_write_key settings_raw_iter_write_val)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_wdt_recovery_trace_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/kernel/mutex.c DIRECTORY ${PBL_BASE}/kernel
      APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    foreach(pair "fw/services/filesystem|pfs.c" "drivers/flash|flash_api.c"
                 "drivers/flash|flash_erase.c")
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source} DIRECTORY ${PBL_BASE}/${directory}
        APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    foreach(source settings_file.c settings_raw_iter.c)
      set_property(SOURCE ${PBL_BASE}/fw/services/settings/${source}
        DIRECTORY ${PBL_BASE}/fw/services/settings APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    set_property(SOURCE ${PBL_BASE}/subsys/task_wdt/task_wdt.c
      DIRECTORY ${PBL_BASE}/subsys/task_wdt APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/fw/services/blob_db/settings_blob_db.c
      DIRECTORY ${PBL_BASE}/fw/services/blob_db APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/fw/services/blob_db/endpoint2.c
      DIRECTORY ${PBL_BASE}/fw/services/blob_db APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_wdt_recovery_trace_call_boundaries)
endif()

if(CONFIG_T5AI_WDT_CAUSE_TRACE)
  pbl_library_sources(wdt_cause_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wdt_cause_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  foreach(symbol pbl_task_wdt_suspend pbl_task_wdt_resume pbl_task_wdt_feed_all
                 pbl_task_wdt_add pbl_task_wdt_feed pbl_task_wdt_feed_thread)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_wdt_cause_trace_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/subsys/task_wdt/task_wdt.c
      DIRECTORY ${PBL_BASE}/subsys/task_wdt APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/fw/services/settings/settings_file.c
      DIRECTORY ${PBL_BASE}/fw/services/settings APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_wdt_cause_trace_call_boundaries)
endif()

if(CONFIG_T5AI_BG_TRACE)
  pbl_library_sources(bg_trace.c)
  foreach(symbol sched_block pbl_mutex_lock_lr pbl_mutex_unlock
                 system_task_timer_init)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_bg_trace_call_boundaries)
    # Observe internal timeout wakeups as well as external object wakeups.
    # GCC instrumentation supplies state boundaries without editing common code.
    file(READ ${PBL_BASE}/kernel/sched.c scheduler)
    string(REGEX MATCHALL "[a-zA-Z_][a-zA-Z_0-9]*\\(" names "${scheduler}")
    set(excluded "")
    foreach(name IN LISTS names)
      string(REPLACE "(" "" name "${name}")
      if(NOT name MATCHES "^(sched_block|sched_wake|sched_tick|sched_switch_in)$")
        list(APPEND excluded ${name})
      endif()
    endforeach()
    list(REMOVE_DUPLICATES excluded)
    list(JOIN excluded "," excluded)
    set_property(SOURCE ${PBL_BASE}/kernel/sched.c DIRECTORY ${PBL_BASE}/kernel
      APPEND PROPERTY COMPILE_OPTIONS -fno-lto -finstrument-functions
      -finstrument-functions-exclude-function-list=${excluded})
    foreach(source mutex.c sem.c msgq.c poll.c thread.c arch/arm/arch.c)
      set_property(SOURCE ${PBL_BASE}/kernel/${source} DIRECTORY ${PBL_BASE}/kernel
        APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    set_property(SOURCE ${PBL_BASE}/fw/services/system_task/service.c
      DIRECTORY ${PBL_BASE}/fw/services/system_task APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_bg_trace_call_boundaries)
endif()

if(CONFIG_T5AI_APP_TRACE)
  pbl_library_sources(app_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/app_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tick.c
    APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  pbl_link_options(-Wl,--wrap=pbl_msgq_get)
  function(t5ai_app_trace_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/fw/process_management/process_manager.c
      DIRECTORY ${PBL_BASE}/fw/process_management APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_app_trace_call_boundaries)
endif()

if(CONFIG_T5AI_SDK_COMPAT_CACHE)
  pbl_library_sources(sdk_compat.c)
  foreach(symbol process_manager_process_setup process_manager_process_cleanup
                 process_manager_compiled_with_legacy2_sdk
                 process_manager_compiled_with_legacy3_sdk)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_sdk_compat_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/fw/process_management/process_manager.c
      DIRECTORY ${PBL_BASE}/fw/process_management APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_sdk_compat_call_boundaries)
endif()

if(CONFIG_T5AI_PIXEL_FAST_PATH)
  pbl_library_sources(pixel_fast.c)
  function(t5ai_pixel_fast_call_boundaries)
    # Rename the stock definition without disabling LTO in the graphics library.
    set_property(SOURCE ${PBL_BASE}/fw/applib/graphics/graphics_private.c
      DIRECTORY ${PBL_BASE}/fw/applib APPEND PROPERTY COMPILE_DEFINITIONS
      graphics_private_plot_pixel=t5ai_stock_graphics_private_plot_pixel)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_pixel_fast_call_boundaries)
endif()

if(CONFIG_T5AI_TASK_CPU_METRICS)
  pbl_library_sources(task_cpu_metrics.c)
  pbl_link_options(-Wl,--wrap=system_task_timer_init)
  function(t5ai_task_cpu_metrics_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/fw/services/system_task/service.c
      DIRECTORY ${PBL_BASE}/fw/services/system_task APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL t5ai_task_cpu_metrics_call_boundaries)
endif()
