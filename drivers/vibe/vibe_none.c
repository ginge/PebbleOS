/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/vibe.h>

static int prv_init(const struct pbl_vibe *vibe) {
  return 0;
}
static int prv_start(const struct pbl_vibe *vibe, int8_t strength) {
  return 0;
}
static int prv_stop(const struct pbl_vibe *vibe) {
  return 0;
}
static const struct pbl_vibe_ops s_ops = {
  .init = prv_init,
  .start = prv_start,
  .stop = prv_stop,
};
PBL_VIBE_STATE_DEFINE(s_vibe);
static const struct pbl_vibe s_vibe =
    PBL_VIBE_INIT(s_vibe, "unavailable-vibe", nullptr, &s_ops, VIBE_STRENGTH_OFF);
const struct pbl_vibe *const VIBE = &s_vibe;
PBL_DEVICE_REGISTER(s_vibe, &s_vibe.dev);
