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
pbl_library_sources_ifdef(CONFIG_T5AI_SLEEP_GUARD_TEST sleep_guard_test.c)
pbl_library_sources_ifdef(CONFIG_T5AI_NATIVE_ALARM native_alarm.c)
pbl_library_sources_ifdef(CONFIG_T5AI_NATIVE_ALARM_TEST native_alarm_test.c)
pbl_library_sources_ifdef(CONFIG_T5AI_TICK_PROFILE tick_profile.c)
pbl_library_sources_ifdef(CONFIG_T5AI_DISPLAY_PROFILE display_profile.c)
pbl_library_sources_ifdef(CONFIG_T5AI_STATE_TRACE state_trace.c)
pbl_library_sources_ifdef(CONFIG_T5AI_REBOOT_TRACE reboot_trace.c)
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

if(CONFIG_T5AI_REMINDER_PROFILE)
  pbl_library_sources(reminder_profile.c)
  foreach(symbol pbl_settings_rewrite_filtered pbl_fs_open pbl_fs_close pbl_flash_read
                 pbl_flash_write pbl_flash_erase pbl_mutex_lock_lr
                 t5ai_flash_ipc_write sys_pbl_analytics_add pbl_mutex_unlock
                 timeline_item_storage_flush)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  # Preserve call boundaries for the diagnostic linker wrappers only.
  function(t5ai_reminder_profile_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/kernel/mutex.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    foreach(pair
        "pebble/services/blob_db|timeline_item_storage.c"
        "subsys/settings|settings.c"
        "subsys/settings|raw_iter.c"
        "subsys/fs|pfs/pfs.c"
        "subsys/fs|pfs/flash_translation.c"
        "drivers/flash|flash.c"
        "drivers/flash|t5ai_flash_ipc.c")
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source}
        DIRECTORY ${PBL_BASE}/${directory} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_reminder_profile_call_boundaries)
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
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_interrupt_profile_call_boundaries)
endif()

if(CONFIG_T5AI_HEAP_TRACE)
  pbl_library_sources(heap_trace.c)
  foreach(symbol pbl_heap_alloc pbl_heap_zalloc pbl_heap_calloc pbl_heap_realloc pbl_heap_free pbl_heap_oom)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_heap_trace_call_boundaries)
    foreach(source heap.c malloc.c)
      set_property(SOURCE ${PBL_BASE}/kernel/${source}
        DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_heap_trace_call_boundaries)
endif()

if(CONFIG_T5AI_FAULT_TRACE OR CONFIG_T5AI_RETAINED_SLEEP_TEST)
  pbl_library_sources(fault_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/fault_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
endif()

if(CONFIG_T5AI_FAULT_TRACE)
  function(t5ai_fault_trace_call_boundaries)
    # Keep diagnostic entry points separate from the native fault handlers.
    set_property(SOURCE ${PBL_BASE}/kernel/arch/arm/fault.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_DEFINITIONS
      HardFault_Handler=t5ai_stock_HardFault_Handler
      MemManage_Handler=t5ai_stock_MemManage_Handler
      BusFault_Handler=t5ai_stock_BusFault_Handler
      UsageFault_Handler=t5ai_stock_UsageFault_Handler)
    set_property(SOURCE ${PBL_BASE}/kernel/arch/arm/vector_table.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_fault_trace_call_boundaries)
endif()

if(CONFIG_T5AI_BG_PICK_TRACE)
  pbl_library_sources(bg_pick_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bg_pick_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  pbl_link_options(-Wl,--wrap=sched_switch_in )
  function(t5ai_bg_pick_trace_call_boundaries)
    foreach(source sched.c arch/arm/arch.c)
      set_property(SOURCE ${PBL_BASE}/kernel/${source} DIRECTORY ${PBL_BASE}/kernel
        APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_bg_pick_trace_call_boundaries)
endif()

if(CONFIG_T5AI_WDT_RECOVERY_TRACE)
  if(CONFIG_T5AI_REMINDER_PROFILE)
    message(FATAL_ERROR "Select one settings linker-wrapper profiler at a time")
  endif()
  pbl_include_directories(${PBL_BASE}/subsys/settings)
  pbl_library_sources(wdt_recovery_trace.c settings_sync_trace.c flash_phase_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wdt_recovery_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/settings_sync_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_phase_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  pbl_link_options(-Wl,--wrap=pbl_task_wdt_add -Wl,--wrap=settings_blob_db_mark_all_dirty)
  foreach(symbol pbl_fs_open pbl_fs_close prefs_private_lock alerts_preferences_lock
                 pbl_mutex_lock_lr pbl_mutex_unlock pbl_flash_read pbl_flash_write
                 pbl_flash_erase pbl_settings_mark_all_dirty
                 pbl_settings__raw_iter_write_header pbl_settings__raw_iter_write_key pbl_settings__raw_iter_write_val)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_wdt_recovery_trace_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/kernel/mutex.c DIRECTORY ${PBL_BASE}/kernel
      APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    foreach(pair "subsys/fs|pfs/pfs.c" "subsys/fs|fs.c" "drivers/flash|flash.c")
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source} DIRECTORY ${PBL_BASE}/${directory}
        APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    foreach(source settings.c raw_iter.c)
      set_property(SOURCE ${PBL_BASE}/subsys/settings/${source}
        DIRECTORY ${PBL_BASE}/subsys/settings APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    set_property(SOURCE ${PBL_BASE}/subsys/task_wdt/task_wdt.c
      DIRECTORY ${PBL_BASE}/subsys/task_wdt APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/pebble/services/blob_db/settings_blob_db.c
      DIRECTORY ${PBL_BASE}/pebble/services/blob_db APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/pebble/services/blob_db/endpoint2.c
      DIRECTORY ${PBL_BASE}/pebble/services/blob_db APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_wdt_recovery_trace_call_boundaries)
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
    set_property(SOURCE ${PBL_BASE}/subsys/settings/settings.c
      DIRECTORY ${PBL_BASE}/subsys/settings APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_wdt_cause_trace_call_boundaries)
endif()

if(CONFIG_T5AI_BG_TRACE)
  pbl_library_sources(bg_trace.c)
  foreach(symbol sched_block pbl_mutex_lock_lr pbl_mutex_unlock
                )
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
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_bg_trace_call_boundaries)
endif()

if(CONFIG_T5AI_APP_TRACE)
  pbl_library_sources(app_trace.c)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/app_trace.c
    APPEND PROPERTY COMPILE_OPTIONS -mgeneral-regs-only -fno-lto)
  set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tick.c
    APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  pbl_link_options(-Wl,--wrap=pbl_msgq_get)
  function(t5ai_app_trace_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/pebble/process_management/process_manager.c
      DIRECTORY ${PBL_BASE}/pebble/process_management APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_app_trace_call_boundaries)
endif()

if(CONFIG_T5AI_PIXEL_FAST_PATH)
  pbl_library_sources(pixel_fast.c)
  function(t5ai_pixel_fast_call_boundaries)
    # Rename the stock definition without disabling LTO in the graphics library.
    set_property(SOURCE ${PBL_BASE}/pebble/applib/graphics/graphics_private.c
      DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_DEFINITIONS
      graphics_private_plot_pixel=t5ai_stock_graphics_private_plot_pixel)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_pixel_fast_call_boundaries)
endif()

if(CONFIG_T5AI_TASK_CPU_METRICS)
  pbl_library_sources(task_cpu_metrics.c)

  function(t5ai_task_cpu_metrics_call_boundaries)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_task_cpu_metrics_call_boundaries)
endif()

if(CONFIG_T5AI_DRAW_PROFILE)
  pbl_library_sources(draw_profile.c)
  foreach(symbol window_render graphics_draw_text pbl_mutex_lock_lr SysTick_Handler sched_block)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_draw_profile_boundaries)
    foreach(pair
        "pebble/applib|ui/window.c"
        "pebble/applib|graphics/text_layout.c"
        "kernel|mutex.c"
        "kernel|sched.c"
        "kernel|arch/arm/vector_table.c"
        "soc/t5ai|tick.c"
)
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source}
        DIRECTORY ${PBL_BASE}/${directory} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
    if(CONFIG_T5AI_DRAW_PROFILE_DETAIL)
    foreach(pair
        "pebble/applib|ui/menu_layer.c"
        "pebble/apps/system|launcher/default/app_glance_structured.c"
        "pebble/apps/system|timeline/text_node.c")
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source}
        DIRECTORY ${PBL_BASE}/${directory} APPEND PROPERTY COMPILE_OPTIONS
        -fno-lto -finstrument-functions -finstrument-functions-exclude-file-list=.h)
    endforeach()
    endif()
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_draw_profile_boundaries)
endif()

if(CONFIG_T5AI_TOUCH_QUEUE_COALESCE)
  pbl_library_sources(touch_queue.c)
  # Owned samples cross public event/queue boundaries as callback envelopes.
  # Expand Main tokens after events.c has completed callback accounting.
  foreach(symbol event_put event_take_timeout pbl_msgq_put pbl_msgq_get
      recognizer_manager_handle_touch_event)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_touch_queue_boundaries)
    set_property(SOURCE ${PBL_BASE}/pebble/kernel/events.c
      DIRECTORY ${PBL_BASE}/pebble/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/kernel/msgq.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/pebble/applib/ui/recognizer/recognizer_manager.c
      DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${PBL_BASE}/pebble/applib/ui/recognizer/pan.c
      DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_DEFINITIONS
      sys_get_ticks=t5ai_touch_sample_ticks)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_touch_queue_boundaries)
endif()

if(CONFIG_T5AI_COMPOSITOR_FAST_PATH)
  pbl_library_sources(compositor_fast.c)
  function(t5ai_compositor_fast_boundaries)
    set_property(SOURCE ${PBL_BASE}/pebble/applib/graphics/8_bit/bitblt_private.c
      DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_DEFINITIONS
      bitblt_bitmap_into_bitmap_tiled=t5ai_stock_bitblt_bitmap_into_bitmap_tiled)
    set_property(SOURCE ${PBL_BASE}/pebble/services/compositor/compositor.c
      DIRECTORY ${PBL_BASE}/pebble/services/compositor APPEND PROPERTY COMPILE_DEFINITIONS
      memset=t5ai_compositor_memset)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_compositor_fast_boundaries)
endif()

if(CONFIG_T5AI_TEXT_NODE_CACHE)
  pbl_library_sources(text_node_cache.c)
  function(t5ai_text_node_cache_boundaries)
    set_property(SOURCE ${PBL_BASE}/pebble/apps/system/timeline/text_node.c
      DIRECTORY ${PBL_BASE}/pebble/apps/system APPEND PROPERTY COMPILE_DEFINITIONS
      graphics_text_layout_get_max_used_size=t5ai_text_node_cached_size)
    set_property(SOURCE ${PBL_BASE}/pebble/applib/ui/window.c
      DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_DEFINITIONS
      window_render=t5ai_cache_stock_window_render)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_text_node_cache_boundaries)
endif()

if(CONFIG_T5AI_IO_PROFILE)
  pbl_library_sources(io_profile.c)
endif()

# Keep stock PFS and its NOR implementation intact on every other target.
if(CONFIG_T5AI_SD_PFS)
  pbl_library_sources(sd_pfs.c)
  # GNU --wrap needs unresolved calls at link time, including with release LTO.
  function(t5ai_sd_pfs_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/subsys/fs/pfs/pfs.c
      DIRECTORY ${PBL_BASE}/subsys/fs APPEND PROPERTY COMPILE_DEFINITIONS
      pbl_malloc_check=t5ai_sd_pfs_malloc_check pbl_free=t5ai_sd_pfs_free)
    foreach(pair "subsys/fs|pfs/pfs.c" "subsys/fs|pfs/flash_translation.c")
      string(REPLACE "|" ";" parts "${pair}")
      list(GET parts 0 directory)
      list(GET parts 1 source)
      set_property(SOURCE ${PBL_BASE}/${directory}/${source}
        DIRECTORY ${PBL_BASE}/${directory} APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_sd_pfs_call_boundaries)
  foreach(symbol pfs_init ftl_read ftl_write ftl_erase_sector ftl_erase_subsector
                 ftl_get_size ftl_format ftl_populate_region_list)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
endif()

# Reject mailbox-owner logs before shared log-state/PULSE locks without changing
# stock Pebble logging. Source renaming preserves this boundary with release LTO.
pbl_library_sources(mailbox_logging.c)
function(t5ai_mailbox_logging_boundary)
  set_property(SOURCE ${PBL_BASE}/pebble/kernel/kernel_applib_state.c
    DIRECTORY ${PBL_BASE}/pebble/kernel APPEND PROPERTY COMPILE_DEFINITIONS
    kernel_applib_get_log_state=t5ai_stock_get_log_state)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_mailbox_logging_boundary)

if(CONFIG_T5AI_RETAINED_SLEEP_DEADLINE_PROFILE_ONLY)
  pbl_library_sources(sleep_deadline_profile.c)
  foreach(symbol pbl_timer_start pbl_timer_stop)
    pbl_link_options(-Wl,--wrap=${symbol})
  endforeach()
  function(t5ai_sleep_profile_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/kernel/timer.c
      DIRECTORY ${PBL_BASE}/kernel APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_sleep_profile_call_boundaries)
endif()

pbl_link_options(-Wl,--wrap=pbl_console_lock -Wl,--wrap=pbl_console_unlock)
function(t5ai_console_call_boundaries)
  set_property(SOURCE ${PBL_BASE}/subsys/console/console.c
    DIRECTORY ${PBL_BASE}/subsys/console APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  set_source_files_properties(${PBL_BASE}/subsys/logging/backends/flash_storage.c
    DIRECTORY ${PBL_BASE}/subsys/logging PROPERTIES HEADER_FILE_ONLY TRUE)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_console_call_boundaries)
pbl_library_sources(log_storage.c)

pbl_library_sources(reset.c)

# Idle rechecks the scheduler after queuing CP coordination work.
target_include_directories(soc__t5ai PRIVATE ${PBL_BASE}/kernel ${PBL_BASE}/kernel/arch ${PBL_BASE}/subsys/fs/pfs)

pbl_library_sources(process_regions.c)
function(t5ai_process_regions_source)
  set_source_files_properties(${PBL_BASE}/pebble/process_management/process_regions.c
    DIRECTORY ${PBL_BASE}/pebble/process_management PROPERTIES HEADER_FILE_ONLY TRUE)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_process_regions_source)
