# SPDX-License-Identifier: Apache-2.0
"""Complete delay translation unit with real MMIO and an independent AON endpoint."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    "pbl/soc/t5ai/core.h": """
#include <stdint.h>
static uint32_t mask;
#define __get_PRIMASK() mask
#define __disable_irq() (mask = 1u)
#define __enable_irq() (mask = 0u)
""",
    "pbl/soc/t5ai/rtc.h": """
#include_next <pbl/soc/t5ai/rtc.h>
static uint32_t test_aon(void);
#undef T5AI_AON_RTC_COUNT_LOW
#define T5AI_AON_RTC_COUNT_LOW test_aon()
""",
}
HARNESS = r"""
#include <assert.h>
#include <setjmp.h>
#include <sys/mman.h>
#include "soc/t5ai/delay.c"
static jmp_buf fatal;
static unsigned expected_mask, calls;
static uint32_t aon, aon_step, timer_step=26u;
static bool stuck;
static uint32_t test_aon(void) {
  ++calls;
  if (!stuck && (T5AI_TIMER0_READ_CONTROL & 1u)) {
    T5AI_TIMER0_READ_CONTROL &= ~1u;
    T5AI_TIMER0_READ_VALUE += timer_step;
  }
  uint32_t value=aon;aon+=aon_step;return value;
}
[[noreturn]] void pbl_panic_bare_at(const char *file, int line) {
  (void)file;(void)line;assert(mask==expected_mask);longjmp(fatal,1);
}
int main(void) {
  assert(mmap((void *)T5AI_TIMER0_BASE,4096,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=(void *)-1);
  assert(mmap((void *)T5AI_SYSTEM_BASE,4096,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=(void *)-1);
  delay_init();assert(T5AI_TIMER0_PERIOD==UINT32_MAX && (T5AI_TIMER0_START&1u));
  assert((T5AI_SYSTEM_TIMER0_ENABLE&(1u<<4)) && (T5AI_SYSTEM_TIMER0_CLOCK&(1u<<20)));
  for (unsigned initial=0;initial<2;++initial) {
    mask=initial;expected_mask=initial;calls=0;
    delay_us(0);assert(!calls && mask==initial);
    T5AI_TIMER0_READ_VALUE=UINT32_MAX-100u;
    uint32_t start=T5AI_TIMER0_READ_VALUE;
    delay_us(20);assert(T5AI_TIMER0_READ_VALUE-start>=20u*T5AI_TIMER0_CYCLES_PER_US);
    assert(mask==initial);
    // A permanently latched request terminates with a live or stopped AON.
    for (unsigned live=0;live<2;++live) {
      mask=initial;stuck=true;aon=UINT32_MAX-5u;aon_step=live;calls=0;
      if (!setjmp(fatal)) {delay_us(1);assert(false);}
      assert(calls <= (live ? 40u : T5AI_TIMER0_POLL_LIMIT+2u));
      assert(mask==initial);
    }
    // Successful latches cannot hide a frozen timer, even with a frozen AON.
    for (unsigned live=0;live<2;++live) {
      mask=initial;stuck=false;timer_step=0;aon=UINT32_MAX-5u;aon_step=live;calls=0;
      if (!setjmp(fatal)) {delay_us(1);assert(false);}
      assert(calls < 3u*T5AI_TIMER0_POLL_LIMIT && mask==initial);
    }
    timer_step=26u;aon_step=0;
  }
  return 0;
}
"""


class DelayTests(unittest.TestCase):
    def test_timing_wrap_latch_stall_and_interrupt_restore(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            for name, content in HEADERS.items():
                header = path / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(content)
            (path / "test.c").write_text(HARNESS)
            subprocess.run(
                [
                    "clang-21",
                    "-std=gnu23",
                    "-D_GNU_SOURCE",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-pie",
                    "-no-pie",
                    "-I",
                    str(path),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
