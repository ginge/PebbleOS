/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

extern volatile uint32_t *pebble_lld_con_env[];
extern void lld_con_max_lat_calc(uint32_t link);

// Called only at the scheduler's two connection-parameter application sites.
void pebble_latency_interval_applied(uint32_t link) {
  lld_con_max_lat_calc(link);
  // Skipped events were scheduled with the old interval. Keep their timestamp
  // and count until event start; data_tx must not rewind them using the new one.
  volatile uint8_t *env = (volatile uint8_t *)pebble_lld_con_env[link];
  env[150] = 0u;
}
