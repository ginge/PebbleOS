# SPDX-License-Identifier: Apache-2.0
"""Exercise native sensor identity/setup retries without accepting permanent faults."""

import unittest

from tools.tests.native_t5ai_harness import run

QMI_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
static unsigned resets,reads,sleeps,fail_resets,fail_identity;
static bool fail_setup;
static bool prv_write(uint8_t reg,uint8_t value){
 if(reg==0x60){assert(value==0xb0);return ++resets>fail_resets;}
 assert((reg==2 && value==0x70)||(reg==9 && value==0x80)||(reg==8 && !value));return !fail_setup;
}
static bool prv_read(uint8_t reg,uint8_t *data,size_t count){assert(reg<=1 && count==1);++reads;*data=reg?0x42:(resets<=fail_identity?0:5);return true;}
static void pbl_sleep_ms(unsigned ms){assert(ms==10);++sleeps;}
"""
QMI_MAIN = r"""
int main(void){
 for(fail_resets=0;fail_resets<=3;++fail_resets){resets=reads=sleeps=0;bool ok=probe();assert(ok==(fail_resets<3));assert(resets==(fail_resets<3?fail_resets+1:3) && sleeps==resets);assert(reads==(ok?2u:0u));}
 fail_resets=0;
 for(fail_identity=0;fail_identity<=3;++fail_identity){resets=reads=sleeps=0;bool ok=probe();assert(ok==(fail_identity<3));assert(resets==(fail_identity<3?fail_identity+1:3) && reads==2*resets);}
 fail_identity=0;fail_setup=true;resets=reads=sleeps=0;assert(!probe() && resets==3 && reads==6);
 return 0;
}
"""
TOUCH_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static uint8_t report[10];static unsigned reads,resets,errors,fail_probes;
static bool prv_read_report(uint8_t *data){assert(data==report);return ++reads>fail_probes;}
static void prv_reset(bool run){assert(run);++resets;}
#define PBL_LOG_ERR(...) (++errors)
"""
TOUCH_MAIN = r"""
int main(void){for(fail_probes=0;fail_probes<=3;++fail_probes){reads=resets=errors=0;
 bool ok=probe();assert(ok==(fail_probes<3));assert(reads==(fail_probes<3?fail_probes+1:3));assert(resets==(fail_probes<2?fail_probes:2));assert(errors==!ok);}
 return 0;}
"""


def qmi_probe(source):
    defines = "\n".join(
        line for line in source.splitlines() if line.startswith("#define QMI8658_")
    )
    start = source.index("  bool ok = false;", source.index("static int prv_init("))
    end = source.index("  pbl_mutex_unlock(&s_dev->state->io_lock);", start)
    return (
        defines + "\nstatic bool probe(void){\n" + source[start:end] + "\nreturn ok;}"
    )


def touch_probe(source):
    start = source.index(
        "  bool detected = false;", source.index("static int prv_touch_init(")
    )
    end = source.index("  prv_reset(false);", start)
    return "static bool probe(void){\n" + source[start:end] + "\nreturn detected;}"


class SensorStartupTest(unittest.TestCase):
    def test_imu_retries_bus_and_identity_failures_but_rejects_permanent_faults(self):
        run(
            "drivers/imu/qmi8658/qmi8658_t5ai.c",
            QMI_PREFIX,
            QMI_MAIN,
            transform=qmi_probe,
        )

    def test_touch_retries_probe_and_reset_with_a_fixed_budget(self):
        run(
            "drivers/touch/cst92xx/cst92xx_t5ai.c",
            TOUCH_PREFIX,
            TOUCH_MAIN,
            transform=touch_probe,
        )
