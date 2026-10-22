/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>

#include <pbl/drivers/temperature.h>

static int prv_read(const struct pbl_temp *temp, int32_t *millicelsius) {
  return -ENODEV;
}

static const struct pbl_temp_ops s_ops = {.read = prv_read};
PBL_TEMP_STATE_DEFINE(s_temp);
static const struct pbl_temp s_temp =
    PBL_TEMP_INIT(s_temp, "unavailable-temp", nullptr, nullptr, &s_ops);
const struct pbl_temp *const DIE_TEMP = &s_temp;
PBL_DEVICE_REGISTER(s_temp, &s_temp.dev);
