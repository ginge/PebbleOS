	}

	if(flash_erase_ipc_state != IPC_FLASH_OP_ACK)
		unhygienix_cp_flash_fatal();

	return BK_OK;
}

bk_err_t mb_flash_op_prepare