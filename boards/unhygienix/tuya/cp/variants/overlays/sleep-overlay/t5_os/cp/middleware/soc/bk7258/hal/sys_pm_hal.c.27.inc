		*pd_reg_v = v;
		if (unhygienix_native_sleep_domain_bypass()) return;

		/*config power domain*/