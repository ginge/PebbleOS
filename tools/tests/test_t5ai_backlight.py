# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Exercise the T5AI driver through the real upstream backlight class."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class BacklightTest(unittest.TestCase):
    def test_device_init_brightness_and_refresh(self):
        headers = {
            "pbl/kernel/mutex.h": """#pragma once
struct pbl_mutex { int unused; };
#define PBL_MUTEX_INITIALIZER {0}
#define PBL_FOREVER 0
static inline void pbl_mutex_lock(struct pbl_mutex *m, int t) {(void)m;(void)t;}
static inline void pbl_mutex_unlock(struct pbl_mutex *m) {(void)m;}
""",
            "pbl/device.h": """#pragma once
#include <stdbool.h>
struct pbl_device { bool ready; };
struct pbl_device_class { int unused; };
#define PBL_DEVICE_CLASS_DEFINE(sym,name) const struct pbl_device_class sym={0}
#define PBL_DEVICE_STATE_DEFINE(sym)
#define PBL_DEVICE_INIT_CLASS(...) {true}
#define PBL_DEVICE_REGISTER(sym,dev)
static inline bool pbl_device_is_ready(const struct pbl_device *d) {return d->ready;}
""",
            "pbl/util/misc.h": """#include <stddef.h>
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
""",
            "pbl/drivers/display/t5ai.h": """#pragma once
#include <stdint.h>
#define T5AI_AMOLED_MIN_BRIGHTNESS 8u
void display_co5300_set_brightness(uint8_t);
""",
        }
        source = r'''
#include <assert.h>
#include <stdint.h>
static unsigned writes;
static uint8_t level;
void display_co5300_set_brightness(uint8_t value) { ++writes; level = value; }
#include CLASS_SOURCE
#include DRIVER_SOURCE
int main(void) {
 const struct pbl_backlight *bl = &pbl_backlight_t5ai;
 assert(pbl_backlight_dev_init(&bl->dev) == 0 && writes == 1 && level == 8);
 for (unsigned i = 0; i <= 255; ++i) {
   unsigned clamped = i > 100 ? 100 : i;
   unsigned expected = (clamped * 255 + 99) / 100;
   if (expected < 8) expected = 8;
   assert(pbl_backlight_get_level(bl, i) == expected);
   assert(pbl_backlight_set_brightness(bl, i) == 0);
   assert(level == expected && bl->state->brightness == clamped);
   unsigned previous = writes;
   assert(pbl_backlight_refresh(bl) == 0);
   assert(writes == previous + (clamped != 0));
 }
 assert(pbl_backlight_set_brightness(bl, 0) == 0 && level == 8);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            for name, contents in headers.items():
                path = tmp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
            path = tmp / "test.c"
            path.write_text(source)
            subprocess.run([
                "cc", "-std=gnu2x", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-I", str(tmp), "-I", str(ROOT / "include"),
                f'-DCLASS_SOURCE="{ROOT / "drivers/backlight/backlight.c"}"',
                f'-DDRIVER_SOURCE="{ROOT / "drivers/backlight/t5ai.c"}"',
                str(path), "-o", str(tmp / "test"),
            ], check=True)
            subprocess.run([str(tmp / "test")], check=True)
