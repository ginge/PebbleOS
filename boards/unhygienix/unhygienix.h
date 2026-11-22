#pragma once
#include <stdint.h>
#define BT_VENDOR_ID   0x0EEA
#define BT_VENDOR_NAME "Core Devices LLC"

// Only Back and Select are populated on this board.
#define BT_PAIRING_CONFIRM_BUTTON BUTTON_ID_SELECT
#define BT_PAIRING_DECLINE_BUTTON BUTTON_ID_BACK
