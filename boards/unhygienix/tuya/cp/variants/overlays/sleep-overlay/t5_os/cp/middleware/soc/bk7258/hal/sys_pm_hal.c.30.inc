void sys_hal_enter_normal_sleep(uint32_t peri_clk)
{
	// TODO - find out the reason:
	// When sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask() is called, the normal sleep can't wakedup,
	// need to find out!!!
	// sys_ll_set_cpu0_int_halt_clk_op_cpu0_halt(1);
	if (unhygienix_native_normal_sleep_allowed())
        arch_sleep();
}
