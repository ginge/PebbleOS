# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
if(NOT CONFIG_LINKER_MAP)
  message(FATAL_ERROR "Unhygienix image validation requires CONFIG_LINKER_MAP")
endif()
set(T5AI_CP_STOCK "" CACHE FILEPATH "Hash-verified stock Unhygienix CP image")
function(pbl_unhygienix_firmware)
  set(protected ${PROJECT_BINARY_DIR}/pebbleos.t5ai-crc.bin)
  set(checked ${PROJECT_BINARY_DIR}/pebbleos.image-layout.ok)
  # Record the actual link declarations, including enabled diagnostic wrappers.
  get_target_property(link_options pebbleos LINK_OPTIONS)
  string(REGEX MATCHALL "--wrap=[A-Za-z_][A-Za-z_0-9]*" wraps "${link_options}")
  list(TRANSFORM wraps REPLACE "--wrap=" "")
  list(REMOVE_DUPLICATES wraps)
  list(SORT wraps)
  list(JOIN wraps "\n" wrap_lines)
  set(wrap_manifest ${PROJECT_BINARY_DIR}/t5ai-link-wrappers.txt)
  file(CONFIGURE OUTPUT ${wrap_manifest} CONTENT "${wrap_lines}\n" @ONLY)
  add_custom_command(OUTPUT ${checked}
    COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/check_unhygienix_build_integration.py
            --elf ${PROJECT_BINARY_DIR}/pebbleos.elf --config ${PROJECT_BINARY_DIR}/.config
            --wraps ${wrap_manifest} --nm ${CMAKE_NM}
    COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/check_unhygienix_image_map.py
            ${PROJECT_BINARY_DIR}/pebbleos.map
    COMMAND ${CMAKE_COMMAND} -E touch ${checked}
    DEPENDS pebbleos ${PBL_BASE}/tools/check_unhygienix_image_map.py
            ${PBL_BASE}/tools/check_unhygienix_build_integration.py ${wrap_manifest}
            ${PROJECT_BINARY_DIR}/.config
    WORKING_DIRECTORY ${PBL_BASE} VERBATIM)
  add_custom_command(OUTPUT ${protected}
    COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/unhygienix_image.py
            ${PROJECT_BINARY_DIR}/pebbleos.bin ${protected}
    DEPENDS ${PROJECT_BINARY_DIR}/pebbleos.bin ${checked}
            ${PBL_BASE}/tools/unhygienix_image.py ${PBL_BASE}/tools/t5ai_image.py
    WORKING_DIRECTORY ${PBL_BASE} VERBATIM)
  add_custom_target(pbl_unhygienix_image DEPENDS ${protected})
  add_dependencies(pbl_firmware pbl_unhygienix_image)
  add_custom_target(t5ai_cp
    COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/unhygienix_cp.py
            --stock "${T5AI_CP_STOCK}" --output ${PROJECT_BINARY_DIR}
            --compiler ${CMAKE_C_COMPILER} --objcopy ${CMAKE_OBJCOPY}
    WORKING_DIRECTORY ${PBL_BASE} VERBATIM)
endfunction()
function(pbl_unhygienix_defer_firmware)
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL pbl_unhygienix_firmware)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL pbl_unhygienix_defer_firmware)
