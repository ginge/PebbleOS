# SPDX-License-Identifier: Apache-2.0
"""Real retained backend and shared callers, with modeled cache publication."""

import unittest

from tools.tests.t5ai_retained_harness import ROOT, run_retained

IMAGE = bytes.fromhex(
    (ROOT / "tools/tests/fixtures/t5ai_retained/legacy.hex").read_text()
)
VECTOR = "const unsigned char legacy[] = {" + ",".join(map(str, IMAGE)) + "};\n"
FALLBACK = """
assert(s_retained.magic == T5AI_RETAINED_MAGIC);
assert(s_retained.checksum == prv_checksum());
for (unsigned j = 0; j < T5AI_RETAINED_WORDS; ++j)
  assert(retained_read(j) == (j == T5AI_RTC_EXTERNAL_UNTRUSTED ?
         T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE : 0));
"""


class RetainedInterfaceTest(unittest.TestCase):
    def test_legacy_bytes_and_layout(self):
        run_retained(
            VECTOR
            + """
static_assert(sizeof(s_retained) == 136);
static_assert(offsetof(T5AIRetainedState, values) == 4);
static_assert(offsetof(T5AIRetainedState, checksum) == 132);
load_image(legacy);
t5ai_retained_init();
assert(memcmp((const void *)&s_retained, legacy, sizeof(legacy)) == 0);
assert(retained_read(CURRENT_TIME_REGISTER) == 1800000000u);
assert(retained_read(T5AI_RTC_BASE_TICKS_HIGH) == 1);
assert(retained_read(T5AI_RTC_BASE_TICKS_LOW) == 0x1000);
seed_image();
assert(memcmp((const void *)&s_retained, legacy, sizeof(legacy)) == 0);
"""
        )

    def test_adapters_and_irq_state(self):
        run_retained("""
for (unsigned locked = 0; locked < 2; ++locked) {
  mask = locked;
  for (unsigned i = 0; i < T5AI_RETAINED_WORDS; ++i) {
    retained_storage_write(i, 0xfeed0000u + i);
    assert(retained_storage_read(i) == 0xfeed0000u + i);
    assert(retained_read(i) == 0xfeed0000u + i);
    assert(mask == locked);
    assert(s_retained.checksum == prv_checksum());
  }
  t5ai_retained_write_clock(1900000000u, UINT64_C(0x200003000));
  assert(mask == locked);
}
assert(boot_version_read() == 0);
""")

    def test_corruption_fallback(self):
        run_retained(
            VECTOR
            + """
for (unsigned i = 0; i < sizeof(s_retained) / 4; ++i) {
  load_image(legacy);
  ((volatile uint32_t *)&s_retained)[i] ^= 1;
  t5ai_retained_init();
  assert(s_retained.magic == T5AI_RETAINED_MAGIC);
  assert(s_retained.checksum == prv_checksum());
  for (unsigned j = 0; j < T5AI_RETAINED_WORDS; ++j)
    assert(retained_read(j) == (j == T5AI_RTC_EXTERNAL_UNTRUSTED &&
           i != 1 + T5AI_RTC_EXTERNAL_UNTRUSTED ?
           T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE : 0));
}
"""
        )

    def test_each_clock_publication_cut(self):
        run_retained(
            VECTOR
            + """
load_image(legacy);
injecting = true;
t5ai_retained_write_clock(1900000000u, UINT64_C(0x200003000));
assert(point == 39);
for (unsigned cut = 1; cut <= 39; ++cut) {
  load_image(legacy);
  cut_at = cut;
  injecting = true;
  if (setjmp(lost_power) == 0) {
    t5ai_retained_write_clock(1900000000u, UINT64_C(0x200003000));
    abort();
  }
  injecting = false;
  mask = 0;
  memcpy((void *)&s_retained, durable, sizeof(s_retained));
  t5ai_retained_init();
  if (cut >= 38) {
    assert(retained_read(CURRENT_TIME_REGISTER) == 1900000000u);
    assert(retained_read(T5AI_RTC_BASE_TICKS_HIGH) == 2);
    assert(retained_read(T5AI_RTC_BASE_TICKS_LOW) == 0x3000);
    assert(retained_read(0) == 0x10203040u);
    assert(s_retained.checksum == prv_checksum());
  } else {
"""
            + FALLBACK
            + "\n}\n}\n"
        )

    def test_complete_bootbits_and_reboot_reason_callers(self):
        run_retained("""
boot_bit_init();
assert(boot_bit_test(BOOT_BIT_INITIALIZED));
boot_bit_clear(BOOT_BIT_INITIALIZED);
assert(!boot_bit_test(BOOT_BIT_INITIALIZED));
boot_bit_set(BOOT_BIT_INITIALIZED);
reboot_set_slot_of_last_launched_app(42);
retained_write(T5AI_RTC_EXTERNAL_UNTRUSTED, T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE);
RebootReason first = {0}, result = {0};
first.code = RebootReasonCode_Watchdog;
first.data16 = 0xabcd;
first.watchdog.stuck_task_pc = 0x12345678;
first.watchdog.stuck_task_lr = 0x23456789;
first.watchdog.stuck_task_callback = 0x34567890;
reboot_reason_set(&first);
reboot_reason_get(&result);
assert(memcmp(&first, &result, sizeof(first)) == 0);
reboot_reason_set(&first);
assert(will_set == 2 && did_set == 1 && warnings == 1);
prv_sys_off_prepare(PBL_SYS_OFF_RESTART_PREPARE, NULL);
reboot_reason_get(&result);
assert(result.restarted_safely);
t5ai_retained_init();
assert(reboot_get_slot_of_last_launched_app() == 42);
reboot_reason_clear();
memset(&result, 0xff, sizeof(result));
reboot_reason_get(&result);
RebootReason empty = {0};
assert(memcmp(&result, &empty, sizeof(result)) == 0);
assert(boot_bit_test(BOOT_BIT_INITIALIZED));
assert(retained_read(T5AI_RTC_EXTERNAL_UNTRUSTED) == T5AI_RTC_EXTERNAL_UNTRUSTED_VALUE);
""")

    def test_bounds_assertions(self):
        for index in (32, 255):
            for operation in (
                f"retained_read({index});",
                f"retained_write({index}, 1);",
            ):
                with self.subTest(operation=operation):
                    run_retained(operation, expected=-6)
