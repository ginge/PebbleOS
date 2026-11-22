				case LOW_PWR_CORE_UNHYGIENIX_ALARM_REQUEST:
					unhygienix_alarm_request(&msg);
					break;
				case LOW_PWR_CORE_UNHYGIENIX_ALARM_FIRED:
					unhygienix_alarm_fired(&msg);
					break;
				case LOW_PWR_CORE_UNHYGIENIX_SLEEP_RESUMED:
					unhygienix_sleep_resumed_event(&msg);
					break;
                case LOW_PWR_CORE_STATE_ENTER_DEEPSLEEP: