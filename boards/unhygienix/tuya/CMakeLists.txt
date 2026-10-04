# SPDX-License-Identifier: Apache-2.0
add_library(${EXAMPLE_LIB})
target_sources(${EXAMPLE_LIB} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/hardware_core.c
    ${CMAKE_CURRENT_LIST_DIR}/src/hardware.c
    ${CMAKE_CURRENT_LIST_DIR}/src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/src/os_adapter.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rtc_port.c
    ${CMAKE_CURRENT_LIST_DIR}/src/input_port.c
    ${CMAKE_CURRENT_LIST_DIR}/src/battery_port.c
    ${CMAKE_CURRENT_LIST_DIR}/src/hci_port.c)
target_include_directories(${EXAMPLE_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_CURRENT_LIST_DIR}/pebble_include)

# Match the pinned SDK's application-CPU FreeRTOS configuration and ABI.
set(T5_AP ${PLATFORM_PATH}/t5_os/ap)
set(T5_FREERTOS ${T5_AP}/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel)
target_include_directories(${EXAMPLE_LIB} PRIVATE
    ${PLATFORM_PATH}/t5_os/build/bk7258/tuya_app/bk7258_ap/config
    ${T5_FREERTOS}/include/freertos
    ${T5_FREERTOS}/include/additions
    ${T5_FREERTOS}/portable/GCC/ARM_CM33_NTZ/non_secure
    ${T5_AP}/components/bk_rtos/freertos
    ${T5_AP}/components/cmsis/CMSIS_5/CMSIS/Core/Include
    ${T5_AP}/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Include
    ${T5_AP}/components/cmsis/CMSIS_5/Device/Beken/armstar
    ${T5_AP}/components/bk_rtos/include
    ${T5_AP}/middleware/driver/spinlock
    ${T5_AP}/middleware/soc/bk7258_ap/hal
    ${T5_AP}/middleware/soc/common/soc/include
    ${T5_AP}/middleware/soc/bk7258_ap
    ${T5_AP}/middleware/soc/bk7258_ap/soc
    ${T5_AP}/middleware/arch/cm33/include
    ${T5_AP}/middleware/arch/cm33/os/freertos
    ${T5_AP}/middleware/arch/cm33
    ${T5_AP}/middleware/soc/common/hal/include)

# The pinned vendor port declares two unused static inline functions without bodies.
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/src/os_adapter.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
