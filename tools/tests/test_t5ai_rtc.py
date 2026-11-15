# SPDX-License-Identifier: Apache-2.0
"""Execute the production RTC with a fault-injecting bus and interrupt model."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    "board/board.h": "#define I2C_PCF85063 1\n",
    "pbl/drivers/i2c.h": "",
    "pbl/drivers/rtc.h": "",
    "pbl/soc/t5ai/rtc.h": "",
    "cmsis_core.h": "",
    "pbl/soc/t5ai/cache.h": "#define t5ai_cache_clean test_clean\n",
    "pbl/kernel/assert.h": "#define PBL_ASSERTN(x) assert(x)\n",
}
HEADERS.update(
    {
        "pbl/util/time.h": "",
        "pbl/kernel/time.h": "",
        "pbl/drivers/rtc.h": r"""
struct pbl_device { int (*init)(const struct pbl_device *); };
struct pbl_rtc;
struct pbl_rtc_ops {
  int (*get_time)(const struct pbl_rtc *, time_t *);
  int (*set_time)(const struct pbl_rtc *, time_t);
};
struct pbl_rtc { struct pbl_device dev; const struct pbl_rtc_ops *ops; };
#define PBL_DEVICE_STATE_DEFINE(sym)
#define PBL_DEVICE_REGISTER(sym, dev)
#define PBL_DEVICE_INIT_CLASS(sym, name, init_fn, parent, deps, cls) {.init = init_fn}
""",
    }
)
PRELUDE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#define pbl_tm tm
#define pbl_gmtime_r gmtime_r
#define pbl_localtime_r localtime_r
#define pbl_mktime timegm
#define pbl_strftime_r strftime
#define TIME_STRING_BUFFER_SIZE 80
#define RTC_TICKS_HZ 1000u
#define T5AI_AON_RTC_HZ 32000u
// Keep the actual multiword MMIO sampling code under test.
static uint64_t raw;
static unsigned irq_mask;
static void test_point(void);
static void test_clean(const void *, size_t);
static uint32_t low_read(void) { uint32_t v = raw; test_point(); return v; }
static uint32_t high_read(void) { uint32_t v = raw >> 32; test_point(); return v; }
#define T5AI_AON_RTC_COUNT_LOW low_read()
#define T5AI_AON_RTC_COUNT_HIGH high_read()
#define __get_PRIMASK() irq_mask
#define __disable_irq() (irq_mask = 1)
static void restore_mask(unsigned v);
#define __set_PRIMASK(v) restore_mask(v)
#define __DMB() ((void)0)
#define __DSB() ((void)0)
typedef uint64_t RtcTicks;
typedef struct { uint32_t words[5]; } TimezoneInfo;
static void rtc_get_time_ms(time_t *, uint16_t *);
static time_t rtc_get_time(void);
static uint8_t control, calendar[7];
static int calls, fail_at, fail_also, corrupt_at, corrupt_kind, restarts;
static int operation(void) {
  assert(!irq_mask);
  ++calls;
  return calls == fail_at || calls == fail_also ? -calls : 0;
}
static int pbl_i2c_reg_read_byte(int d, unsigned r, uint8_t *v) {
  (void)d; (void)r; int e = operation(); *v = control;
  if (calls == corrupt_at) *v |= corrupt_kind == 2 ? 0x02 : 0x20;
  return e;
}
static int pbl_i2c_burst_read(int d, unsigned r, uint8_t *v, size_t n) {
  (void)d; (void)r; int e = operation(); memcpy(v, calendar, n);
  if (calls == corrupt_at) v[0] = corrupt_kind == 1 ? 0x02 : corrupt_kind == 3 ? 0x80 : 0x7a;
  return e;
}
static int pbl_i2c_reg_write_byte(int d, unsigned r, uint8_t v) {
  (void)d; (void)r; int e = operation();
  if (!(v & 0x20)) ++restarts;
  if (!e) control = v;
  return e;
}
static int pbl_i2c_burst_write(int d, unsigned r, const uint8_t *v, size_t n) {
  (void)d; (void)r; int e = operation(); memcpy(calendar, v, n); return e;
}
#include RETAINED_SOURCE
#include RTC_SOURCE
static int injection, point, target, pending, active;
static time_t observed;
static void interrupt(void) {
  if (active) return;
  active = 1;
  if (injection == 1) observed = rtc_get_time();
  if (injection == 2) { raw += 32000; rtc_set_time(1800000000); }
  active = 0;
}
static void restore_mask(unsigned v) {
  irq_mask = v;
  if (!irq_mask && pending && !active) { pending = 0; interrupt(); }
}
#include <setjmp.h>
static jmp_buf reset_target;
static unsigned char durable[sizeof(s_retained)];
static unsigned clean_points;
// Model cache loss on reset and individual line writeback in the real clean calls.
static void test_clean(const void *p, size_t n) {
  uintptr_t first = (uintptr_t)p & ~(uintptr_t)31;
  uintptr_t end = ((uintptr_t)p + n + 31) & ~(uintptr_t)31;
  uintptr_t base = (uintptr_t)&s_retained;
  for (uintptr_t line = first; line < end; line += 32) {
    for (uintptr_t at = line; at < line + 32; ++at) {
      if (at >= base && at < base + sizeof(s_retained))
        durable[at - base] = *(const unsigned char *)at;
    }
    if (injection == 5 && ++clean_points == (unsigned)target)
      longjmp(reset_target, 1);
  }
}
static void test_point(void) {
  if (active || !injection || injection == 5) return;
  if (++point != target) return;
  if (injection == 3) longjmp(reset_target, 1);
  if (injection == 4) { raw += 2; return; } // low/high rollover during sampling
  if (irq_mask) pending = 1;
  else interrupt();
}
"""
EXTERNAL = r"""
int main(int argc, char **argv) {
  assert(argc == 2);
  int scenario = atoi(argv[1]);
  rtc_init();
  time_t epoch = 1793318400;
  if (scenario <= 6) {
    fail_at = scenario;
    bool ok = prv_external_rtc_write(epoch);
    assert(ok == (scenario == 0));
    assert(s_external_rtc_ready == ok);
    assert(s_external_rtc_error == -scenario);
    assert(restarts == (scenario >= 1 && scenario <= 2 ? 0 : 1));
    if (scenario == 3) assert(!(control & 0x20));
    assert(!irq_mask);
    fail_at = 0; calls = 0; s_external_rtc_present = true;
    rtc_set_time(epoch + 1); // bounded retry repairs health
    assert(s_external_rtc_ready && s_external_rtc_error == 0);
    assert(rtc_get_time() == epoch + 1);
  } else if (scenario == 7) {
    fail_at = 3; fail_also = 4;
    assert(!prv_external_rtc_write(epoch));
    assert(restarts == 1 && s_external_rtc_error == -3);
  } else if (scenario <= 9) {
    corrupt_at = scenario == 8 ? 5 : 6;
    assert(!prv_external_rtc_write(epoch));
    assert(!s_external_rtc_ready && s_external_rtc_error != 0);
  } else if (scenario <= 11) {
    fail_at = scenario - 9; // boot control/calendar read failure: no writes
    rtc_init_timers();
    assert(!s_external_rtc_ready && !s_external_rtc_present);
    assert(calls == fail_at && !restarts && !irq_mask);
    rtc_set_time(epoch); assert(calls == fail_at);
  } else if (scenario == 12) {
    control = 0x20; // responding stopped clock initialized from fallback
    rtc_init_timers();
    assert(s_external_rtc_ready && !(control & 0x20));
    assert(rtc_get_time() == T5AI_RTC_MIN_VALID_TIME);
  } else if (scenario == 13) {
    assert(prv_external_rtc_write(epoch));
    calls = 0; rtc_init_timers();
    assert(calls == 2 && s_external_rtc_ready && rtc_get_time() == epoch);
  } else if (scenario <= 19) {
    control = 0x20; fail_at = scenario - 11; // each boot repair operation
    rtc_init_timers();
    assert(!s_external_rtc_ready && s_external_rtc_present);
    assert(t5ai_retained_read(T5AI_RTC_EXTERNAL_UNTRUSTED));
    fail_at = 0; calls = 0; rtc_set_time(epoch);
    assert(s_external_rtc_ready && !t5ai_retained_read(T5AI_RTC_EXTERNAL_UNTRUSTED));
  } else if (scenario == 20) {
    rtc_set_time(epoch);
    fail_at = 3; assert(!prv_external_rtc_write(epoch + 100));
    assert(!(control & 0x20)); // plausible wrong time after failed calendar write
    fail_at = 0; calls = 0;
    rtc_init(); rtc_init_timers();
    assert(s_external_rtc_ready && rtc_get_time() == epoch);
    assert(calls == 8); // did not trust the calendar; repaired from retained fallback
    fail_at = 3; calls = 0;
    assert(!prv_external_rtc_write(epoch + 100));
    // An interrupted clock commit must not erase the external fault signature.
    s_retained.magic = 0;
    test_clean(&s_retained, sizeof(s_retained));
    rtc_init();
    assert(t5ai_retained_read(T5AI_RTC_EXTERNAL_UNTRUSTED) ==
           T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE);
    fail_at = 0; calls = 0; rtc_init_timers();
    assert(s_external_rtc_ready && rtc_get_time() == T5AI_RTC_MIN_VALID_TIME);
  } else if (scenario == 21) {
    assert(!prv_external_rtc_write(0)); // out-of-range year: no bus traffic
    assert(!s_external_rtc_ready && calls == 0);
  } else if (scenario <= 24) {
    corrupt_kind = scenario - 21;
    corrupt_at = scenario == 23 ? 5 : 6;
    assert(!prv_external_rtc_write(epoch));
    assert(!s_external_rtc_ready && s_external_rtc_error != 0);
  }
  return 0;
}
"""


class RTCRegressionTest(unittest.TestCase):
    def compile_run(self, harness, scenarios):
        with tempfile.TemporaryDirectory() as temporary:
            d = Path(temporary)
            for name, text in HEADERS.items():
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text)
            # Instrument actual memory operations, including reset after each persistent store.
            retained = (ROOT / "soc/t5ai/retained.c").read_text()
            retained = retained.replace(
                "return s_retained.values[index];",
                "uint32_t v = s_retained.values[index]; test_point(); return v;",
            )
            start = retained.index("void t5ai_retained_write_clock(")
            prefix, transaction = retained[:start], retained[start:]
            lines = transaction.splitlines()
            stores = 0
            for i, line in enumerate(lines):
                if line.strip().startswith("s_retained.") and " = " in line:
                    lines[i] += " test_point();"
                    stores += 1
            assert stores == 6
            (d / "retained.c").write_text(prefix + "\n".join(lines))
            (d / "test.c").write_text(PRELUDE + harness)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu2x",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O2",
                    "-DCONFIG_SOC_T5AI=1",
                    "-I",
                    str(d),
                    "-I",
                    str(ROOT / "include"),
                    "-I",
                    str(ROOT / "pebble"),
                    f'-DRETAINED_SOURCE="{d / "retained.c"}"',
                    f'-DRTC_SOURCE="{ROOT / "drivers/rtc/t5ai.c"}"',
                    str(d / "test.c"),
                    "-o",
                    str(d / "test"),
                ],
                check=True,
            )
            for scenario in scenarios:
                with self.subTest(scenario=scenario):
                    subprocess.run([str(d / "test"), str(scenario)], check=True)

    def test_external_failure_cleanup_and_verified_health(self):
        self.compile_run(EXTERNAL, range(25))


COHERENCE = r"""
int main(int argc, char **argv) {
  assert(argc == 2);
  int mode = atoi(argv[1]);
  raw = 0x1fffffff0ULL;
  rtc_init(); rtc_set_time(1700000000);
  assert(sizeof(s_retained) == 136);
  t5ai_retained_write(RTC_TIMEZONE_GMTOFFSET, 1234);
  t5ai_retained_write(REBOOT_REASON_REGISTER_1, 5678);
  if (mode < 20) {
    // Reader ISR requested at every raw/tuple publication access waits for commit.
    raw += 32000000;
    injection = 1; target = mode;
    rtc_set_time(1700000100);
    if (point >= target) assert(observed == 1700000100);
    assert(!pending && !irq_mask);
  } else if (mode < 40) {
    injection = 2; target = mode - 19;
    time_t seconds; uint16_t ms;
    rtc_get_time_ms(&seconds, &ms);
    assert(seconds == 1700000000 && ms == 0);
    assert(!pending && !irq_mask);
    if (point >= target) assert(rtc_get_time() == 1800000000);
  } else if (mode < 47) {
    // Raw snapshot has four accesses, then six persistence store boundaries.
    target = mode - 40 + 4; injection = 3;
    if (!setjmp(reset_target)) rtc_set_time(1800000000);
    injection = 0; irq_mask = 0;
    memcpy(&s_retained, durable, sizeof(s_retained));
    t5ai_retained_init(); rtc_init();
    time_t result = rtc_get_time();
    assert(result == 1700000000 || result == 1800000000 ||
           result == T5AI_RTC_MIN_VALID_TIME);
    if (mode >= 42 && mode <= 46) assert(result == T5AI_RTC_MIN_VALID_TIME);
  } else if (mode == 47 || mode >= 61) {
    raw = 0x1ffffffffULL; rtc_set_time(1700000000);
    injection = 4; target = mode == 47 ? 1 : mode - 59;
    uint64_t sampled = prv_raw_ticks();
    assert(sampled == 0x200000001ULL || (target == 4 && sampled == 0x1ffffffffULL));
    injection = 0;
    raw += 32000;
    time_t sec; uint16_t ms; rtc_get_time_ms(&sec, &ms);
    assert(sec == 1700000001 && ms == 0);
  } else if (mode == 48) {
    raw = UINT64_MAX - 15999; rtc_set_time(1700000000);
    raw += 32000;
    assert(rtc_get_time() == 1700000001); // modulo raw elapsed across full wrap
  } else if (mode == 49) {
    uint64_t before = rtc_get_ticks();
    rtc_set_time(1800000000); rtc_set_time(1700000000);
    assert(rtc_get_ticks() == before);
    raw += 32000; assert(rtc_get_ticks() == before + 1000);
    irq_mask = 1; rtc_get_time(); rtc_set_time(1700000000);
    assert(irq_mask == 1); // nested/ISR caller's mask is preserved
    assert(t5ai_retained_read(RTC_TIMEZONE_GMTOFFSET) == 1234);
    assert(t5ai_retained_read(REBOOT_REASON_REGISTER_1) == 5678);
  } else if (mode == 50) {
    raw = 0; rtc_init(); // counter reset rebases existing saved epoch
    assert(rtc_get_time() == 1700000000);
  } else {
    injection = 5; target = mode - 50;
    if (!setjmp(reset_target)) rtc_set_time(1800000000);
    injection = 0; irq_mask = 0;
    memcpy(&s_retained, durable, sizeof(s_retained));
    rtc_init();
    time_t result = rtc_get_time();
    assert(result == 1700000000 || result == 1800000000 ||
           result == T5AI_RTC_MIN_VALID_TIME);
  }
  return 0;
}
"""


class RTCCoherenceTest(unittest.TestCase):
    compile_run = RTCRegressionTest.compile_run

    def test_interleaved_tuple_wrap_and_reset(self):
        self.compile_run(COHERENCE, [*range(1, 11), *range(20, 27), *range(40, 64)])
