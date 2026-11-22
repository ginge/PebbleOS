	sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_mac_int_gen_en(0x1);
	if (unhygienix_native_sleep_cp_gpio_owned()) {
#if CONFIG_SPE
		sys_ll_set_cpu0_int_32_63_en_cpu0_gpio_int_en(0x1);
#else
		sys_ll_set_cpu0_int_32_63_en_cpu0_wifi_hsu_irq_en(0x1);
#endif
	}