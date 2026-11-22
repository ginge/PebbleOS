if(NOT CONFIG_WIFI_ENABLE)
    list(APPEND blacklist_elem libble_wifi_exchange[^.]*\.a)
endif()

list (JOIN blacklist_elem | blacklist_elem_regax)