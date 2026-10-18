# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Run the production naked entry on Cortex-M33 with real stack-limit checking."""
import ast
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get("PEBBLE_SDK_PATH", "/home/baz/pebbleos-sdk-0.1.8"))
CC = SDK / "arm-none-eabi/bin/arm-none-eabi-gcc"
QEMU = SDK / "qemu/bin/qemu-pebble"

HARNESS = r"""
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
uint8_t t5ai_fault_stack[512] __attribute__((aligned(8)));
static volatile unsigned seen;
static uint32_t *saved_frame;
static uint32_t saved_exc;
static uint32_t expected_msp;
static void output(const char *text) {
  register uint32_t r0 __asm("r0") = 4;
  register const char *r1 __asm("r1") = text;
  __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
}
__attribute__((noreturn)) static void finish(unsigned status) {
  uint32_t args[2] = {0x20026, status};
  register uint32_t r0 __asm("r0") = 0x20;
  register uint32_t *r1 __asm("r1") = args;
  __asm volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
  while (1) {}
}
#define CHECK(x) do { if (!(x)) { output("FAIL: " #x "\n"); finish(1); } } while(0)
static uint32_t read_msp(void) { uint32_t x; __asm("mrs %0, msp":"=r"(x)); return x; }
static uint32_t read_msplim(void) { uint32_t x; __asm("mrs %0, msplim":"=r"(x)); return x; }
static uint32_t read_primask(void) { uint32_t x; __asm("mrs %0, primask":"=r"(x)); return x; }
void t5ai_fault_trace_capture(uint32_t *frame, uint32_t exc, uint32_t original_limit,
                               uint32_t original_mask) {
  CHECK(original_limit == 0x20003000);
  CHECK(original_mask == ((TESTCASE & 4) ? 1 : 0));
  CHECK(read_msplim() == (uintptr_t)t5ai_fault_stack);
  CHECK(read_msp() >= (uintptr_t)t5ai_fault_stack);
  CHECK(read_msp() < (uintptr_t)t5ai_fault_stack + 512);
  CHECK(read_primask() == 1);
  CHECK(!(REG(0xe000ed14) & (1u<<10))); // Do not suppress HardFault stack limits.
  CHECK(!(REG(0xe000ed28) & (1u<<20))); // No STKOF during entry.
  CHECK(frame[7] & (1u<<24));
  CHECK(exc == ((TESTCASE & 1) ? 0xfffffffd : 0xfffffff9) - ((TESTCASE & 2) ? 16 : 0));
  saved_frame=frame; saved_exc=exc; seen++;
  // The caller must not depend on scratch registers surviving the C call.
  __asm volatile("mov r0,#0\nmov r1,#0\nmov r2,#0\nmov r3,#0\nmov r12,#0" ::: "r0","r1","r2","r3","r12");
}
__attribute__((noreturn)) void verify_stock(uint32_t msp, uint32_t limit,
                                           uint32_t primask, uint32_t exc) {
  CHECK(seen == 1);
  CHECK(limit == 0x20003000);
  CHECK(primask == ((TESTCASE & 4) ? 1 : 0));
  CHECK(exc == saved_exc);
  if (!(TESTCASE & 1)) CHECK(msp == (uintptr_t)saved_frame);
  else CHECK(msp == expected_msp);
  CHECK(!(REG(0xe000ed28) & (1u<<20)));
  output("PASS fault entry\n"); finish(0);
}
__attribute__((naked)) void t5ai_stock_HardFault_Handler(void) {
  __asm volatile("mrs r0,msp\nmrs r1,msplim\nmrs r2,primask\nmov r3,lr\nb verify_stock");
}
void HardFault_Handler(void);
__attribute__((noreturn)) void reset(void) {
  REG(0xe000ed14) &= ~(1u<<10);
  REG(0xe000ed88) |= 0xfu<<20; // Enable FPU.
  __asm volatile("dsb\nisb");
  const uint32_t psp=0x20006000, limit=0x20003000;
  __asm volatile("msr psp,%0\nmsr msplim,%1" ::"r"(psp),"r"(limit):"memory");
  uint32_t control=((TESTCASE & 1) ? 2 : 0) | ((TESTCASE & 2) ? 4 : 0);
  uint32_t mask=(TESTCASE & 4) ? 1 : 0;
  expected_msp=read_msp();
  __asm volatile("msr control,%0\nisb\nmsr primask,%1\nudf #0"::"r"(control),"r"(mask):"memory");
  finish(2);
}
__attribute__((section(".vectors"))) const uintptr_t vectors[] = {
  0x20004000, (uintptr_t)reset, 0, (uintptr_t)HardFault_Handler
};
"""
LINKER = """ENTRY(reset)
MEMORY { FLASH (rx) : ORIGIN = 0, LENGTH = 4M
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 512K }
SECTIONS { .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } >FLASH
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } >RAM }
"""


def entry_assembly(source):
    body = re.search(r"void HardFault_Handler.*?__asm volatile\((.*?)\);", source, re.S)
    if not body:
        raise AssertionError("Production fault entry missing")
    instructions = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', body[1]))
    return ".syntax unified\n.thumb\n.global HardFault_Handler\n.thumb_func\nHardFault_Handler:\n" + instructions


@unittest.skipUnless(CC.exists() and QEMU.exists(), "ARM SDK/QEMU unavailable")
class FaultEntryTests(unittest.TestCase):
    def test_actual_m33_entry_with_stack_limits(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "entry.S").write_text(entry_assembly((ROOT / "soc/t5ai/fault_trace.c").read_text()))
            (tmp / "test.c").write_text(HARNESS)
            (tmp / "test.ld").write_text(LINKER)
            for case in range(8):
                with self.subTest(psp=bool(case & 1), fp=bool(case & 2), masked=bool(case & 4)):
                    subprocess.run([str(CC), "-mcpu=cortex-m33", "-mthumb", "-mgeneral-regs-only",
                                    "-O1", "-nostdlib", "-fno-builtin", "-DTESTCASE=" + str(case),
                                    "-Wl,-T," + str(tmp / "test.ld"), str(tmp / "test.c"),
                                    str(tmp / "entry.S"), "-o", str(tmp / "test.elf")], check=True)
                    result = subprocess.run([str(QEMU), "-M", "pebble-gabbro", "-display", "none",
                                             "-serial", "null", "-monitor", "none", "-semihosting-config",
                                             "enable=on,target=native", "-kernel", str(tmp / "test.elf")],
                                            capture_output=True, text=True, timeout=10,
                                            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("PASS fault entry", result.stdout + result.stderr)

            # Prove the emulator catches the original stack/limit mismatch.
            broken = entry_assembly((ROOT / "soc/t5ai/fault_trace.c").read_text())
            broken = broken.replace("msr msplim, lr\n", "").replace("msr msplim, r12\n", "")
            (tmp / "entry.S").write_text(broken)
            subprocess.run([str(CC), "-mcpu=cortex-m33", "-mthumb", "-mgeneral-regs-only",
                            "-O1", "-nostdlib", "-fno-builtin", "-DTESTCASE=0",
                            "-Wl,-T," + str(tmp / "test.ld"), str(tmp / "test.c"),
                            str(tmp / "entry.S"), "-o", str(tmp / "bad.elf")], check=True)
            result = subprocess.run([str(QEMU), "-M", "pebble-gabbro", "-display", "none",
                                     "-serial", "null", "-monitor", "none", "-semihosting-config",
                                     "enable=on,target=native", "-kernel", str(tmp / "bad.elf")],
                                    capture_output=True, text=True, timeout=10,
                                    preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Lockup", result.stderr)
