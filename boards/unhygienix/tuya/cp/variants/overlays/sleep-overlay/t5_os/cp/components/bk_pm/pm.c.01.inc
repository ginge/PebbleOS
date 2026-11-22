#include <modules/pm.h>
#include <driver/unhygienix_sleep_protocol.h>
extern void unhygienix_native_sleep_stage(uint32_t stage);
extern bool unhygienix_native_sleep_mask_only(void);
extern void unhygienix_native_flash_prepare_mask_only(void);