# SPDX-License-Identifier: Apache-2.0
"""Fault-inject CP hooks and verify their pinned binary patch/relocation contract."""

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import unhygienix_cp_flash as guard
from tools.patch_unhygienix_cp_rf import CP_XIP, compile_hook, install_hook
from tools.t5ai_image import encode_protected_blocks, verify_protected_blocks

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r"""
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
static uint32_t now, status_reg, ack;
static int off, prepare_result, finish_result, fatal, logs;
static jmp_buf recovery;
static FlashTrace trace;
static uint32_t *reg(uintptr_t a) {
 if(a==0x4400020c) return &now;
 if(a==0x2806a738) return &status_reg;
 assert(a==0x2806b36c);return &ack;
}
int stock_cpu_off(unsigned cpu) {assert(cpu==1);return off;}
int stock_prepare(void) {now+=2;return prepare_result;}
int stock_finish(void) {now+=3;return finish_result;}
void pebble_flash_fatal(void) {++fatal;longjmp(recovery,1);}
void stock_protect(unsigned type) {now+=4;}
void stock_status(uint32_t value) {status_reg=value;now+=5;}
unsigned stock_mode(unsigned mode) {now+=6;return 4;}
uint32_t stock_irq_disable(void) {return 1;}
void stock_irq_enable(uint32_t flags) {assert(flags==1);}
void bk_printf(const char *fmt, ...) {assert(!trace.active && !trace.irq_depth);++logs;}
int stock_flash_write(uint32_t address,const uint8_t *buffer,uint32_t size) {
 pebble_flash_prepare();pebble_flash_protect(0);pebble_flash_status(0);
 uint32_t flags=pebble_flash_program_enter();now+=7;
 // Nested critical sections must measure the complete outer interval once.
 uint32_t nested=pebble_flash_irq_enter();now+=8;
 pebble_flash_irq_exit(nested);now+=9;pebble_flash_program_exit(flags);
 pebble_flash_protect(1);pebble_flash_status(0x3c);pebble_flash_mode(4);
 pebble_flash_finish();return -7;
}
int main(void) {
 ack=2;status_reg=0x3c;now=UINT32_MAX-5;
 assert(pebble_flash_write(0,0,8)==-7);
 assert(trace.phase[WRITE].count==1 && trace.bytes==8 && trace.changes==2);
 assert(trace.phase[PROGRAM].count==1 && trace.phase[PROGRAM].max==24);
 assert(!trace.active && !logs);
 for(unsigned i=1;i<128;i++) assert(pebble_flash_write(0,0,8)==-7);
 assert(logs==10 && trace.phase[WRITE].count==128);
 ack=1;
 if(!setjmp(recovery)) {pebble_flash_prepare();assert(0);}
 assert(fatal==1 && trace.failures==1); // Includes stock cap false-success.
 off=1;assert(pebble_flash_prepare()==0); // CP legitimately skips sleeping AP.
 off=0;ack=2;finish_result=-1;
 if(!setjmp(recovery)) {pebble_flash_finish();assert(0);}
 assert(fatal==2 && trace.failures==2);
}
"""


class CPFlashGuardTest(unittest.TestCase):
    def test_volatile_packaging_requires_ap_ownership_even_without_toolchain(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "unsafe"
            with self.assertRaisesRegex(ValueError, "notification only ACKs"):
                guard.install_flash_guard(
                    b"",
                    b"",
                    Path(tmp) / "missing.elf",
                    "missing",
                    "missing",
                    output,
                    True,
                )
            self.assertFalse(output.exists())

    def test_notification_errors_nested_irq_and_timing_wrap(self):
        source = guard.SOURCES[0].read_text()
        source = source.replace(
            "#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))",
            "static uint32_t *reg(uintptr_t);\n#define REG32(a) (*reg(a))",
        )
        source = source.replace(
            "#define TRACE ((volatile FlashTrace *)(uintptr_t)0x28067360)",
            "static FlashTrace trace;\n#define TRACE (&trace)",
        )
        source = source.replace(
            "extern void pebble_flash_fatal(void) __attribute__((noreturn));",
            "extern void pebble_flash_fatal(void);",
        )
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(source + HARNESS)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Wno-unused-parameter",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)

    def test_unknown_elf_and_image_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "wrong.elf"
            p.write_bytes(b"wrong")
            with self.assertRaisesRegex(ValueError, "SHA256"):
                guard.install_flash_guard(b"wrong", b"wrong", p, "cc", "objcopy", tmp)
            stock = ROOT / "build/inputs/stock-cp.bin"
            if stock.exists():
                with self.assertRaisesRegex(ValueError, "exact verified stock ELF"):
                    guard.install_flash_guard(
                        stock.read_bytes(), stock.read_bytes(), p, "cc", "objcopy", tmp
                    )

    def test_real_packaging_preserves_live_code_and_reserves_only_unused_ram(self):
        stock = ROOT / "build/inputs/stock-cp.bin"
        elf = Path(
            "/home/baz/Documents/ChatGPT/pebble/.vendor/tuyaopen/platform/T5AI/t5_os/build/bk7258/tuya_app/bk7258/app.elf"
        )
        compiler = shutil.which("arm-none-eabi-gcc")
        if not stock.exists() or not elf.exists() or not compiler:
            self.skipTest("Pinned stock CP/ELF and ARM compiler needed")
        objcopy = str(Path(compiler).with_name("arm-none-eabi-objcopy"))
        data = stock.read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            hook = compile_hook(CP_XIP + ((len(data) + 15) & ~15), compiler, objcopy)
            rf = install_hook(data, hook)
            damaged = bytearray(rf)
            damaged[128] ^= 1
            with self.assertRaisesRegex(ValueError, "outside the RF hook"):
                guard.install_flash_guard(
                    data, bytes(damaged), elf, compiler, objcopy, tmp
                )
            result, meta = guard.install_flash_guard(
                data, rf, elf, compiler, objcopy, tmp
            )
            self.assertLessEqual(meta["guard_size"], guard.ITCM_LIMIT - guard.ITCM_END)
            patches = {
                i
                for p in meta["patches"]
                for i in range(
                    p["address"] - CP_XIP,
                    p["address"] - CP_XIP + len(bytes.fromhex(p["after"])),
                )
            }
            for i, (before, after) in enumerate(zip(rf, result)):
                if i not in patches:
                    self.assertEqual(before, after, f"unexpected patch at {i:#x}")
            for entry, load, start, end, limit in [
                (
                    guard.COPY_ENTRY,
                    guard.ITCM_LOAD,
                    guard.ITCM_START,
                    guard.ITCM_END,
                    guard.ITCM_LIMIT,
                ),
                (
                    guard.IRAM_COPY,
                    guard.IRAM_LOAD,
                    guard.IRAM_START,
                    guard.IRAM_END,
                    guard.IRAM_LIMIT,
                ),
            ]:
                new_load, new_start, words = struct.unpack_from(
                    "<III", result, entry - CP_XIP
                )
                self.assertEqual((new_start, words), (start, (limit - start) // 4))
                self.assertEqual(
                    result[new_load - CP_XIP : new_load - CP_XIP + end - start],
                    data[load - CP_XIP : load - CP_XIP + end - start],
                )
            iram_load = meta["iram_load"] - CP_XIP
            self.assertEqual(
                result[iram_load + 0x1360 : iram_load + 0x1400], bytes(160)
            )
            self.assertEqual(
                verify_protected_blocks(encode_protected_blocks(result))[: len(result)],
                result,
            )
            # Until AP XIP is genuinely parked, even explicit opt-in must fail
            # before a deployable image or output directory is produced.
            with self.assertRaisesRegex(ValueError, "RAM-resident AP ownership"):
                guard.install_flash_guard(
                    data, rf, elf, compiler, objcopy, Path(tmp) / "spi", True
                )
            self.assertFalse((Path(tmp) / "spi").exists())
            self.assertIsNone(meta["spi_replacement"])
            # Offline transport compilation remains available for audit. It is
            # not evidence of AP ownership or device acceptance of a command.
            built = guard.compile_guard(
                CP_XIP + ((len(rf) + 15) & ~15), compiler, objcopy, Path(tmp), True
            )
            self.assertLessEqual(len(built["spi_guard"]), 0x238)
            # Decode every immediate branch; no flash literal/veneer may be used
            # to enter the guard after a controller command has launched.
            for site, base, out, shift in guard.WAITS:
                h1, h2, bx = struct.unpack_from("<HHH", result, site - CP_XIP)
                target = (
                    (h1 & 15) << 12
                    | ((h1 >> 10) & 1) << 11
                    | ((h2 >> 12) & 7) << 8
                    | (h2 & 255)
                )
                self.assertEqual(h1 & 0xFBF0, 0xF240)
                self.assertEqual((h2 >> 8) & 15, out)
                self.assertEqual(bx, 0x4780 | out << 3)
                self.assertTrue(
                    target & 1 and guard.ITCM_END <= target < guard.ITCM_LIMIT
                )
