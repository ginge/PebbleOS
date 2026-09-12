# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

from tools.t5ai_image import build_pebble_flash_image

from .core import Runner, RunnerCaps, RunnerError


class TyuToolRunner(Runner):
    name = "tyutool"

    def __init__(self, cfg, tty=None, tool="tyutool_cli"):
        super().__init__(cfg)
        self.tty = tty
        self.tool = tool

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, flash_resources=True)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument("--tyutool-tty", help="Serial port for the tyutool runner")
        parser.add_argument("--tyutool", default="tyutool_cli", help="Path to tyutool_cli")

    @classmethod
    def do_create(cls, cfg, args):
        return cls(cfg, tty=getattr(args, "tyutool_tty", None),
                   tool=getattr(args, "tyutool", "tyutool_cli"))

    def do_run(self, command):
        if not self.tty:
            raise RunnerError("Port not specified, use --tyutool-tty")
        if not self.cfg.bin_file or not self.cfg.resources_file:
            raise RunnerError("T5AI flashing requires firmware and system resources")

        output = Path(self.cfg.bin_file).with_name("pebbleos.t5ai-flash.bin")
        image = build_pebble_flash_image(
            Path(self.cfg.bin_file).read_bytes(),
            Path(self.cfg.resources_file).read_bytes(),
        )
        if not self.cfg.dry_run:
            output.write_bytes(image)

        start = 0x132000
        end = start + len(image)
        cmd = self.quote(self.tool, "write", "-d", "t5ai", "-p", self.tty,
                         "-s", hex(start), "--end", hex(end), "-f", output, "--plain")
        if self.call(cmd):
            raise RunnerError("tyutool failed to flash the T5AI AP image")
