bk_err_t unhygienix_native_sleep_notify_ap(uint32_t generation, uint32_t tick)
{
    mb_chnl_cmd_t command = {0};
    command.hdr.cmd = T5AI_PM_NATIVE_SLEEP_RESUMED;
    command.param1 = generation;
    command.param2 = tick;
    command.param3 = T5AI_PM_NATIVE_ALARM_ABI;
    // One attempt only: no queue, retries or RTOS delay while CP IRQs are masked.
    return mb_chnl_write(MB_CHNL_PWC, &command);
}

bk_err_t bk_pm_cp0_response_cp1(uint32_t cmd, uint32_t param1,uint32_t param2,uint32_t param3)