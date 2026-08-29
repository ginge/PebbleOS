/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include "task.h"
#include <cmsis_core.h>

void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks) {
  (void)expected_idle_ticks;
  __DSB();
  __WFI();
  __ISB();
}

bool vPortEnableTimer(void) {
  return false;
}
