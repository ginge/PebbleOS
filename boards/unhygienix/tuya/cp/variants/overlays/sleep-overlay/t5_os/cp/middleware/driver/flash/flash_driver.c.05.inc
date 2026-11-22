static bool s_flash_is_init = false;

extern volatile bool unhygienix_native_sleep_prepared;
extern bool unhygienix_native_sleep_mask_only(void);
static struct {
    bool valid;
    flash_line_mode_t line_mode;
    uint32_t quad_bits;
    const volatile uint32_t *probe_address;
    uint32_t probe_value;
} s_native_flash_resume;

static inline __attribute__((always_inline)) bool unhygienix_native_flash_irqs_locked(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    return primask != 0u;
}

static inline __attribute__((always_inline)) void unhygienix_native_flash_barrier(void)
{
    __asm volatile("dsb sy\n\tisb sy" ::: "memory");
}
