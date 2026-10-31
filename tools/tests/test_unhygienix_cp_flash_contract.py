# SPDX-License-Identifier: Apache-2.0
"""Exercise the carried-forward controller flash contract, without SPI bypass."""

from io import BytesIO
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from elftools.elf.elffile import ELFFile
from tools import unhygienix_cp_build as build
from tools import unhygienix_cp_variant as variant

try:
    from unicorn import (
        Uc,
        UC_ARCH_ARM,
        UC_MODE_THUMB,
        UC_MODE_MCLASS,
        UC_HOOK_CODE,
        UC_HOOK_MEM_READ,
        UC_HOOK_MEM_WRITE,
    )
    from unicorn.arm_const import (
        UC_ARM_REG_R0,
        UC_ARM_REG_SP,
        UC_ARM_REG_LR,
        UC_ARM_REG_PC,
    )
except ImportError:
    Uc = None


class NotificationContract(unittest.TestCase):
    def test_actual_staged_send_requires_ack_and_fails_closed(self):
        if not shutil.which("cc"):
            self.skipTest("requires native C compiler")
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / "sdk"
            import json

            for e in json.loads((build.BOARD / "overlay.json").read_text()):
                dst = stage / e["path"]
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(build.SDK / e["path"], dst)
            build.overlay(stage)
            path = stage / "t5_os/cp/middleware/driver/flash/flash_notify.c"
            text = path.read_text()
            functions = (
                "#if 1\n"
                + text[
                    text.index("static bk_err_t send_flash_op_state") : text.index(
                        "static void mb_flash_ipc_tx_cmpl_isr"
                    )
                ]
            )
            code = (
                r"""
#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
typedef int bk_err_t;
typedef uint32_t u32;
#define CONFIG_CPU_CNT 2
#define BK_OK 0
#define BK_FAIL -1
#define MB_CHNL_FLASH 75
#define FLASH_WAIT_ACK_TIMEOUT 5000
enum {IPC_FLASH_OP_COMPLETE, IPC_FLASH_OP_REQ, IPC_FLASH_OP_ACK};
enum {IPC_FLASH_OP_START, IPC_FLASH_OP_END};
typedef struct { struct {unsigned data,cmd;} hdr;unsigned param1;} mb_chnl_cmd_t;
static volatile uint32_t flash_erase_ipc_state;
static int powered_off,write_error,sends,clock_reads,ack_after,ack_value;
static uint64_t now,step;
static jmp_buf stopped;
static int mb_ipc_cpu_is_power_off(u32 id) {assert(id==1);return powered_off;}
static int mb_chnl_write(int channel,mb_chnl_cmd_t *cmd) {
  assert(channel==75);assert(cmd->param1==IPC_FLASH_OP_REQ);++sends;return write_error;
}
static uint64_t bk_aon_rtc_get_us(void) {
  ++clock_reads;if(ack_after && clock_reads>=ack_after)flash_erase_ipc_state=ack_value;
  now+=step;return now;
}
static void unhygienix_cp_flash_fatal(void) __attribute__((noreturn));
static void unhygienix_cp_flash_fatal(void) {longjmp(stopped,1);}
"""
                + functions
                + r"""
static void reset(void) {
  powered_off=write_error=sends=clock_reads=ack_after=0;now=step=0;
  ack_value=IPC_FLASH_OP_ACK;flash_erase_ipc_state=IPC_FLASH_OP_COMPLETE;
}
static void failure(int finish) {
  if(!setjmp(stopped)) {if(finish)mb_flash_op_finish();else mb_flash_op_prepare();assert(0);}
}
int main(void) {
  reset();powered_off=1;assert(mb_flash_op_prepare()==0);assert(mb_flash_op_finish()==0);assert(sends==0);
  reset();ack_after=1;assert(mb_flash_op_prepare()==0);assert(sends==1);
  reset();ack_after=3;step=100;assert(mb_flash_op_finish()==0);assert(clock_reads==3);
  reset();write_error=-7;failure(0);assert(sends==1 && clock_reads==0);
  reset();step=1000;failure(0);assert(clock_reads<=8);
  reset();failure(0);assert(clock_reads==2001);
  reset();ack_after=1;ack_value=IPC_FLASH_OP_COMPLETE;failure(1);assert(clock_reads==2001);
  reset();step=1000;failure(1);assert(clock_reads<=8);
}
"""
            )
            source = Path(tmp) / "ack.c"
            binary = Path(tmp) / "ack"
            source.write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


@unittest.skipUnless(
    Uc and os.getenv("UNHYGIENIX_CP_ON_WORK") and os.getenv("UNHYGIENIX_CP_OFF_WORK"),
    "requires Unicorn and linked profiles",
)
class LinkedRamContract(unittest.TestCase):
    def test_ram_wait_launch_timeout_wrap_and_fail_stop(self):
        for wifi in ["off", "on"]:
            root = (
                Path(os.environ["UNHYGIENIX_CP_" + wifi.upper() + "_WORK"])
                / "build/bk7258/tuya_app/bk7258"
            )
            elf = ELFFile(BytesIO((root / "app.elf").read_bytes()))

            def addr(name):
                return (
                    variant.symbol(elf, "unhygienix_cp_flash_" + name)["st_value"] & ~1
                )

            itcm = elf.get_section_by_name(".itcm")
            self.assertLessEqual(itcm["sh_addr"] + itcm["sh_size"], 0x4000)
            for launch, clear_after, wrap in [
                (False, 0, False),
                (False, 4, False),
                (True, 5, False),
                (False, None, False),
                (False, None, True),
            ]:
                with self.subTest(
                    wifi=wifi, launch=launch, clear_after=clear_after, wrap=wrap
                ):
                    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
                    for start, size in [
                        (0, 0x4000),
                        (0x02020000, 0x1000),
                        (0x28000000, 0x10000),
                        (0x44000000, 0x1000),
                        (0x44800000, 0x1000),
                        (0x47000000, 0x1000),
                    ]:
                        u.mem_map(start, size)
                    u.mem_write(itcm["sh_addr"], itcm.data())
                    state = {
                        "reads": 0,
                        "aon": 0xFFFF0000 if wrap else 0,
                        "launches": 0,
                        "fatal": False,
                    }

                    def mem_read(u, access, address, size, value, data):
                        if address == 0x4400020C:
                            state["aon"] = (state["aon"] + 4000) & 0xFFFFFFFF
                            u.mem_write(address, struct.pack("<I", state["aon"]))
                        elif address == 0x47000010:
                            state["reads"] += 1
                            busy = clear_after is None or state["reads"] <= clear_after
                            u.mem_write(
                                address,
                                struct.pack("<I", 0x80000000 if busy else 0x1234),
                            )

                    def mem_write(u, access, address, size, value, data):
                        if address == 0x47000010:
                            self.assertTrue(value & 0x20000000)
                            state["launches"] += 1

                    def code(u, pc, size, data):
                        if pc == 0x02020000:
                            u.emu_stop()
                            return
                        if pc == addr("fatal"):
                            state["fatal"] = True
                        if bytes(u.mem_read(pc, 2)) == b"\x30\xbf":
                            u.emu_stop()
                            return
                        self.assertTrue(
                            itcm["sh_addr"] <= pc < itcm["sh_addr"] + itcm["sh_size"]
                        )

                    u.hook_add(UC_HOOK_MEM_READ, mem_read)
                    u.hook_add(UC_HOOK_MEM_WRITE, mem_write)
                    u.hook_add(UC_HOOK_CODE, code)
                    u.reg_write(UC_ARM_REG_R0, 0x47000000)
                    u.reg_write(UC_ARM_REG_SP, 0x28008000)
                    u.reg_write(UC_ARM_REG_LR, 0x02020001)
                    u.emu_start(
                        addr("launch" if launch else "wait") | 1, 0, count=10000
                    )
                    self.assertEqual(state["fatal"], clear_after is None)
                    self.assertEqual(state["launches"], int(launch))
                    if clear_after is None:
                        self.assertEqual(
                            struct.unpack("<I", u.mem_read(0x44000000, 4))[0]
                            & 0x7F000000,
                            0x7E000000,
                        )
                        self.assertEqual(
                            struct.unpack("<I", u.mem_read(0x44800010, 4))[0],
                            0x00A5000A,
                        )
                        self.assertEqual(
                            struct.unpack("<I", u.mem_read(0x44000600, 4))[0],
                            0x00A5000A,
                        )
                    else:
                        self.assertEqual(u.reg_read(UC_ARM_REG_PC), 0x02020000)
                        if not launch:
                            self.assertEqual(u.reg_read(UC_ARM_REG_R0), 0x1234)


if __name__ == "__main__":
    unittest.main(verbosity=2)
