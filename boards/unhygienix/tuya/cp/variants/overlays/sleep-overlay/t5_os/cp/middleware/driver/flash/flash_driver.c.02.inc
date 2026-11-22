    uint32_t quad_mode_bits = 0;
    if (FLASH_LINE_MODE_FOUR == new_line_mode) {
        quad_mode_bits = s_flash.flash_cfg->coutinuous_read_mode_bits_val;
        flash_set_qe();
    }

	flash_hal_clear_qwfr(&s_flash.hal);   // cmd CRMR (coutinuous_read_mode reset), quit QPI mode.