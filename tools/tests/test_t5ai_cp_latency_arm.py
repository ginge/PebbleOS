# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Replay pinned vendor ARM instructions using Unicorn (optional dependency).

Set PBL_CP_LATENCY_BASE and PBL_CP_LATENCY_FIXED to the reviewed base and
fix-only packaged CP binaries. External hardware helpers are
stubbed; scheduler, TX rewind and compiled correction execute unchanged.
"""

import os
import struct
import unittest
from pathlib import Path

try:
    from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
    from unicorn.arm_const import (
        UC_ARM_REG_LR,
        UC_ARM_REG_PC,
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_R2,
        UC_ARM_REG_SP,
    )
except ImportError:
    Uc = None
from tools import patch_unhygienix_cp_latency as packager

ENV = 0x28070554


@unittest.skipUnless(
    Uc and os.getenv("PBL_CP_LATENCY_BASE") and os.getenv("PBL_CP_LATENCY_FIXED"),
    "requires Unicorn and reviewed base/fixed CP paths",
)
class Replay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stock = Path(os.environ["PBL_CP_LATENCY_BASE"]).read_bytes()
        cls.fixed = Path(os.environ["PBL_CP_LATENCY_FIXED"]).read_bytes()
        packager.verify_controller(cls.stock)
        # Fix-only images leave the complete transmit function untouched.
        start = packager.DATA_TX_XIP - packager.CP_XIP
        assert (
            cls.stock[start : start + packager.DATA_TX_BYTES]
            == cls.fixed[start : start + packager.DATA_TX_BYTES]
        )

    def machine(self, fixed):
        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for a, n in (
            (0x02020000, 0x100000),
            (0x28000000, 0x80000),
            (0x60000000, 0x20000),
        ):
            u.mem_map(a, n)
        u.mem_write(0x02020000, self.fixed if fixed else self.stock)
        u.mem_write(0x2806E62C, struct.pack("<I", ENV))
        self.u = u
        self.assertions = 0
        self.removes = 0
        self.inserts = 0
        self.maxcalc = 0
        self.fail_insert = False

        def hook(u, a, size, data):
            value = 0
            if a == 0x02021000:
                u.emu_stop()
                return
            if a == 0x020B0914:
                self.assertions += 1
                u.emu_stop()
                return
            if a == 0x020BDB24:
                self.removes += 1
            elif a == 0x020BD7F0:
                self.inserts += 1
                value = int(self.fail_insert and self.inserts == 1)
            elif a == 0x020B7760:
                self.maxcalc += 1
            elif a == 0x020B543C:
                value = 100
            elif a == 0x020B4C3C:
                value = 414005
            elif a in (
                0x020AF94C,
                0x020AF95C,
                0x020BF690,
                0x020B7F60,
                0x020B7620,
                0x020D8418,
            ):
                pass
            else:
                return
            u.reg_write(UC_ARM_REG_R0, value)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

        u.hook_add(UC_HOOK_CODE, hook)
        return u

    def put(self, off, value, fmt="I"):
        self.u.mem_write(ENV + off, struct.pack("<" + fmt, value))

    def get(self, off, fmt="I"):
        return struct.unpack(
            "<" + fmt, self.u.mem_read(ENV + off, struct.calcsize(fmt))
        )[0]

    def call(self, addr, r0=1, r1=414005, r2=0):
        for reg, val in (
            (UC_ARM_REG_R0, r0),
            (UC_ARM_REG_R1, r1),
            (UC_ARM_REG_R2, r2),
            (UC_ARM_REG_SP, 0x28020000),
            (UC_ARM_REG_LR, 0x02021001),
        ):
            self.u.reg_write(reg, val)
        self.u.emu_start(addr | 1, 0, count=100000)
        self.assertEqual(
            self.u.reg_read(UC_ARM_REG_PC),
            0x02021000 if not self.assertions else 0x020B0914,
        )

    def setup_scheduler(self, second=False, wrap=False, instant=True):
        event = 65520 if wrap else 4640
        self.put(56, 48)
        self.put(60, 19200)
        self.put(66, 0, "H")
        self.put(72, (event + 24) & 65535, "H")
        self.put(74, int(instant), "B")
        self.put(76, 413520)
        self.put(84, 413424)
        self.put(92, 413424)
        self.put(100, 96)
        self.put(104, 19200)
        self.put(114, 22 if second else 30, "H")
        self.put(120, event, "H")
        self.put(122, 1, "H")
        self.put(130, 0x573, "H")
        self.put(142, 1, "B")
        self.fail_insert = second

    def test_captured_failure(self):
        for fixed in (False, True):
            with self.subTest(fixed=fixed):
                self.machine(fixed)
                self.setup_scheduler()
                self.call(0x020B7B68)
                self.assertEqual(
                    (self.get(76), self.get(100), self.get(122, "H")), (415728, 48, 24)
                )
                self.assertEqual(self.get(150, "B"), int(not fixed))
                self.assertEqual(self.maxcalc, 1)
                self.call(0x020B8C84, r1=0x28030000)
                if fixed:
                    self.assertEqual(
                        (
                            self.assertions,
                            self.removes,
                            self.get(76),
                            self.get(122, "H"),
                        ),
                        (0, 0, 415728, 24),
                    )
                else:
                    self.assertEqual(
                        (
                            self.assertions,
                            self.removes,
                            self.get(76),
                            self.get(122, "H"),
                        ),
                        (1, 1, 414576, 0),
                    )

    def test_both_scheduler_paths_and_event_counter_wrap(self):
        for second in (False, True):
            for wrap in (False, True):
                with self.subTest(second=second, wrap=wrap):
                    self.machine(True)
                    self.setup_scheduler(second, wrap)
                    self.call(0x020B7B68)
                    expected = (
                        (416400, 48, 31, 0)
                        if wrap and not second
                        else (415728, 48, 24, 0)
                    )
                    self.assertEqual(
                        (
                            self.get(76),
                            self.get(100),
                            self.get(122, "H"),
                            self.get(150, "B"),
                        ),
                        expected,
                    )
                    self.assertEqual(self.inserts, 2 if second else 1)
                    self.assertEqual(self.maxcalc, 1)
                    self.call(0x020B8C84, r1=0x28030000)
                    self.assertEqual(self.assertions, 0)
                    event = 65520 if wrap else 4640
                    increment = self.get(122, "H")
                    # Execute event-start counter consumption up to its next phase.
                    self.u.reg_write(UC_ARM_REG_R0, ENV)
                    self.u.reg_write(UC_ARM_REG_SP, 0x28020000)
                    self.u.emu_start(0x020B78C1, 0x020B792C, count=1000)
                    self.assertEqual(self.u.reg_read(UC_ARM_REG_PC), 0x020B792C)
                    self.assertEqual(self.get(120, "H"), (event + increment) & 65535)
                    self.assertEqual(self.get(122, "H"), 0)

    def test_normal_latency_still_rewinds(self):
        for fixed in (False, True):
            with self.subTest(fixed=fixed):
                self.machine(fixed)
                self.setup_scheduler(instant=False)
                self.call(0x020B7B68)
                self.assertEqual(
                    (self.get(76), self.get(122, "H"), self.get(150, "B")),
                    (416400, 31, 1),
                )
                self.call(0x020B8C84, r1=0x28030000)
                self.assertEqual(self.assertions, 0)
                self.assertEqual(self.removes, 1)
                self.assertEqual(self.get(76), 414096)
                self.assertEqual(self.get(122, "H"), 7)


if __name__ == "__main__":
    unittest.main(verbosity=2)
