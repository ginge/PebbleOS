set(srcs cp_main.c unhygienix_cp_contract.c unhygienix_ble_rf_init.c unhygienix_flash_contract.S)
if(CONFIG_WIFI_VNET_CONTROLLER)
    list(APPEND srcs customer_msg.c)
endif()