# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the board's fake peripherals through the native driver classes."""

import unittest
from pathlib import Path

from tools.tests.test_t5ai_timing import run_c

ROOT = Path(__file__).resolve().parents[2]


class PeripheralStubTests(unittest.TestCase):
    def test_native_als_and_vibe_contracts(self):
        board = (ROOT / "boards/unhygienix/unhygienix.c").read_text()
        stubs = board[board.index("static int prv_als_stub_init") : board.index("// Shared touch/RTC/IMU bus")]
        headers = {
            "pbl/kernel/mutex.h": """#pragma once
struct pbl_mutex { unsigned unused; };
#define PBL_MUTEX_INITIALIZER {0}
#define PBL_FOREVER 0
static void pbl_mutex_lock(struct pbl_mutex *m,int t) {(void)m;(void)t;}
static void pbl_mutex_unlock(struct pbl_mutex *m) {(void)m;}
""",
            "pbl/device.h": """#pragma once
#include <stdbool.h>
struct pbl_device { bool ready; };
struct pbl_device_class { int unused; };
#define PBL_DEVICE_CLASS_DEFINE(sym,name) const struct pbl_device_class sym={0}
#define PBL_DEVICE_STATE_DEFINE(sym)
#define PBL_DEVICE_INIT(...) {true}
#define PBL_DEVICE_INIT_CLASS(...) {true}
#define PBL_DEVICE_REGISTER(sym,dev)
static bool pbl_device_is_ready(const struct pbl_device *d) {return d->ready;}
""",
            "pbl/kernel/assert.h": "#include <assert.h>\n#define PBL_ASSERTN(x) assert(x)\n",
            "pbl/logging/logging.h": "#define PBL_LOG_MODULE_DEFINE(...)\n#define PBL_LOG_ERR(...) ((void)0)\n",
            "pbl/util/misc.h": "#include <stddef.h>\n#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))\n",
        }
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define nullptr NULL
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define prv_lock als_lock
#define prv_unlock als_unlock
"""
        harness += '#include "' + str(ROOT / "drivers/als/als.c") + '"\n'
        harness += "#undef prv_lock\n#undef prv_unlock\n"
        harness += '#include "' + str(ROOT / "drivers/vibe/vibe.c") + '"\n'
        harness += stubs
        harness += r"""
int main(void) {
  const struct pbl_als *als=&T5AI_ALS;
  assert(pbl_als_dev_init(&als->dev)==0);
  pbl_als_prime(als); pbl_als_suspend(als);
  uint32_t level=123;
  assert(pbl_als_read(als,&level)==0 && level==0);
  assert(!pbl_als_has_lux(als) && pbl_als_to_lux(als,level)==0);
  pbl_als_resume(als); pbl_als_release(als);
  assert(!als->state->prime_count && !als->state->suspend_count);
  const struct pbl_vibe *vibe=&T5AI_VIBE;
  assert(pbl_vibe_dev_init(&vibe->dev)==0);
  assert(pbl_vibe_start(vibe,100)==0 && pbl_vibe_start(vibe,-100)==0);
  assert(pbl_vibe_stop(vibe)==0 && pbl_vibe_get_braking_strength(vibe)==0);
  uint8_t cali=0;
  assert(pbl_vibe_calibrate(vibe,&cali)==-ENOTSUP);
  assert(pbl_vibe_set_calibration(vibe,0)==-ENOTSUP);
  return 0;
}
"""
        run_c(harness, headers)
        roles = (ROOT / "boards/unhygienix/roles.yml").read_text()
        self.assertIn('als:\n  symbol: "&T5AI_ALS"', roles)
        self.assertIn('vibe:\n  symbol: "&T5AI_VIBE"', roles)


if __name__ == "__main__":
    unittest.main()
