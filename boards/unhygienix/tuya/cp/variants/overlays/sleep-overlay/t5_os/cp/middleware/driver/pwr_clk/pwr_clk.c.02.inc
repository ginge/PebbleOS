		case T5AI_PM_NATIVE_ALARM_COMMAND:
			ret = pm_cp0_send_msg(LOW_PWR_CORE_UNHYGIENIX_ALARM_REQUEST, cmd_buf->param1, cmd_buf->param2, cmd_buf->param3);
			break;
		case PM_WAKEUP_CONFIG_CMD: