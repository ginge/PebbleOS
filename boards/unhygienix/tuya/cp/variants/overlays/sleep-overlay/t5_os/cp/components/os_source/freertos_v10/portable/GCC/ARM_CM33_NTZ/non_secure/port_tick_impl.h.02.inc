void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
    extern void unhygienix_native_sleep_port_progress(uint32_t progress);
    unhygienix_native_sleep_port_progress(T5AIPort_Enter);
    extern bool unhygienix_native_sleep_tickless_allowed(void);
    if (!unhygienix_native_sleep_tickless_allowed()) {
        unhygienix_native_sleep_port_progress(T5AIPort_GuardRefused);
        return; // SysTick/IRQs are untouched; the caller resumes its scheduler.
    }
