set(priv_requires_list bk_common bk_log bk_cli os_source)
    if("$ENV{UNHYGIENIX_CP_WIFI}" STREQUAL "on")
        list(APPEND priv_requires_list lwip_intf_v2_1)
    endif()