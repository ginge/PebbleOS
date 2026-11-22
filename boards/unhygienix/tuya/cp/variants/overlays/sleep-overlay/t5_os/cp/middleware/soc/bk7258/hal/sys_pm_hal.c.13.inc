	if (unhygienix_native_sleep_mask_required()) {
        unhygienix_native_sleep_mask_enter();
        sys_ll_set_cpu0_int_halt_clk_op_cpu0_int_mask(1);
    }