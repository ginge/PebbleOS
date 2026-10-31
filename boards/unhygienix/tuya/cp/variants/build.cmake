# SPDX-License-Identifier: Apache-2.0
if(CONFIG_PSA_MBEDTLS AND NOT CONFIG_LWIP)
    armino_component_get_property(cp_crypto psa_mbedtls COMPONENT_LIB)
    target_compile_definitions(${cp_crypto} PUBLIC
        MBEDTLS_USER_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/unhygienix_mbedtls.h")
endif()
target_link_libraries(${CMAKE_PROJECT_NAME}.elf "-Wl,--undefined=unhygienix_cp_contract")
armino_build_set_property(COMPILE_OPTIONS
    "-ffile-prefix-map=$ENV{UNHYGIENIX_CP_STAGE_ROOT}=vendor-t5ai"
    APPEND)
