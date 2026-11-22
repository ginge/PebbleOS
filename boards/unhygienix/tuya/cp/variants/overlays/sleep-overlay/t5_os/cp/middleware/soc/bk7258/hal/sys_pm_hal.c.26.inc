static inline void sys_hal_set_sleep_condition(void)
{
	uint32_t v = sys_ll_get_cpu_power_sleep_wakeup_value();

	//sleep_en_need_cpu2_wfi = 1
	//sleep_bus_idle_bypass =  0
	//sleep_en_global = 1
	//sleep_en_need_cpu0_wfi = 0
	//sleep_en_need_cpu1_wfi = 1
	//sleep_en_need_flash_idle = 0

	#if CONFIG_PM_LV_SUBCORES_ON
	v |= (0x8 << 16);
	#else
	v |= (0x2a << 16);
	#endif

	sys_ll_set_cpu_power_sleep_wakeup_value(unhygienix_native_sleep_condition(v));
}
