    bss_size = _bss_end - _bss_start;
    .unhygienix_sleep_trace (NOLOAD) : ALIGN(32) {
        KEEP(*(.unhygienix_sleep_trace))
    } > RAM