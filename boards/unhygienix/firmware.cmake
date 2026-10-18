# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
if(NOT CONFIG_LINKER_MAP)
  message(FATAL_ERROR "Unhygienix image validation requires CONFIG_LINKER_MAP")
endif()
set(T5AI_CP_STOCK "" CACHE FILEPATH "Hash-verified stock Unhygienix CP image")
function(pbl_unhygienix_firmware)
  set(protected ${PROJECT_BINARY_DIR}/pebbleos.t5ai-crc.bin)
  set(checked ${PROJECT_BINARY_DIR}/pebbleos.image-layout.ok)
  add_custom_command(OUTPUT ${checked}
    COMMAND ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/check_unhygienix_image_map.py
            ${PROJECT_BINARY_DIR}/pebbleos.map
    COMMAND ${CMAKE_COMMAND} -E touch ${checked}
    DEPENDS pebbleos ${PBL_BASE}/tools/check_unhygienix_image_map.py
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
cmake_language(DEFER DIRECTORY ${PBL_BASE} CALL pbl_unhygienix_firmware)
