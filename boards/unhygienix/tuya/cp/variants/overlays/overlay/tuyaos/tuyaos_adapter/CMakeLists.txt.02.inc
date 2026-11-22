if(CONFIG_LWIP)
    list(APPEND srcs src/system/tkl_network.c)
endif()

if ("${CONFIG_CPU_INDEX}" STREQUAL "0")