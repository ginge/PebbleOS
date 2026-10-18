# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/hardware_core.c
    ${CMAKE_CURRENT_LIST_DIR}/src/hardware.c
    ${CMAKE_CURRENT_LIST_DIR}/src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rtc_port.c
    ${CMAKE_CURRENT_LIST_DIR}/src/input_port.c
    ${CMAKE_CURRENT_LIST_DIR}/src/battery_port.c)
target_include_directories(${EXAMPLE_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_CURRENT_LIST_DIR}/pebble_include)
