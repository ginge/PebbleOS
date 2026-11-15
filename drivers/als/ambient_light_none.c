/* SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com> */
/* SPDX-License-Identifier: Apache-2.0 */
#include <pbl/drivers/als.h>

static int prv_init(const struct pbl_als *als) {
  return 0;
}

static int prv_read(const struct pbl_als *als, uint32_t *level) {
  *level = 0;
  return 0;
}

static const struct pbl_als_ops s_ops = {.init = prv_init, .read = prv_read};
PBL_ALS_STATE_DEFINE(s_no_als);
static const struct pbl_als s_no_als = PBL_ALS_INIT(s_no_als, "no-als", nullptr, &s_ops, nullptr);
PBL_DEVICE_REGISTER(s_no_als, &s_no_als.dev);
const struct pbl_als *const ALS = &s_no_als;
