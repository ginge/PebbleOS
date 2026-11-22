		if (s_pm_lowvol_enter_exit_cb_conf[PM_SLEEP_CB_EXIT_LOWVOL_INDEX][dev_id].cb != NULL)
		{
			unhygienix_native_sleep_stage(T5AISleepStage_CP_CallbackEnterBase + dev_id);