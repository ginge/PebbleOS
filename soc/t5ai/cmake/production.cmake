# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
# Production interception contracts: docs/boards/unhygienix/build-integration.md

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
    # Redirect the two callers; keep the stock entry and translation unit intact.
    foreach(source bitblt.c graphics_bitmap.c)
      set_property(SOURCE ${PBL_BASE}/pebble/applib/graphics/${source}
        DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_DEFINITIONS
        bitblt_bitmap_into_bitmap_tiled=t5ai_compositor_bitmap_into_bitmap_tiled)
    endforeach()
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
    # App and transition callers opt into a render-scoped cache; stock stays intact.
    foreach(source app.c ui/window_stack_animation_rect.c ui/window_stack_animation_round.c)
      set_property(SOURCE ${PBL_BASE}/pebble/applib/${source}
        DIRECTORY ${PBL_BASE}/pebble/applib APPEND PROPERTY COMPILE_DEFINITIONS
        window_render=t5ai_text_node_cache_window_render)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_text_node_cache_boundaries)
endif()

# Kconfig explicitly selects the SD FTL and its private page-flags provider.
if(CONFIG_T5AI_SD_PFS)
  pbl_library_sources(sd_pfs.c)
  # Retain the pre/post-init header guard and publication boundary with LTO.
  function(t5ai_sd_pfs_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/subsys/fs/pfs/pfs.c
      DIRECTORY ${PBL_BASE}/subsys/fs APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_sd_pfs_call_boundaries)
  pbl_link_options(-Wl,--wrap=pfs_init)
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

if(CONFIG_CONSOLE)
  pbl_link_options(-Wl,--wrap=pbl_console_lock -Wl,--wrap=pbl_console_unlock)
  function(t5ai_console_call_boundaries)
    set_property(SOURCE ${PBL_BASE}/subsys/console/console.c
      DIRECTORY ${PBL_BASE}/subsys/console APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL t5ai_console_call_boundaries)
endif()
pbl_library_sources(log_storage.c)

pbl_library_sources(reset.c)

# Idle rechecks the scheduler after queuing CP coordination work.
target_include_directories(soc__t5ai PRIVATE ${PBL_BASE}/kernel ${PBL_BASE}/kernel/arch ${PBL_BASE}/subsys/fs/pfs)

pbl_library_sources(process_regions.c)

include(${CMAKE_CURRENT_LIST_DIR}/backends.cmake)
