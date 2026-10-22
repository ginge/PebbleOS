# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

from tools.flash_unhygienix_cp_firmware import validate_firmware as validate_cp
from tools.flash_unhygienix_firmware import validate_current_version, validate_firmware
from tools.unhygienix_image import (
    PEBBLE_AP_PHYSICAL_OFFSET,
    PEBBLE_FILESYSTEM_PHYSICAL_OFFSET,
    PRIMARY_CP_APP_PHYSICAL_OFFSET,
    build_empty_pfs,
    build_pebble_flash_image,
)

from .core import Runner, RunnerCaps, RunnerError


class TyuToolRunner(Runner):
    name = "tyutool"

    def __init__(self, cfg, tty=None, tool="tyutool_cli", provision=False, cp=False):
        super().__init__(cfg)
        self.tty = tty
        self.tool = tool
        self.provision = provision
        self.cp = cp

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, flash_resources=True)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument("--tyutool-tty", help="Serial port for the tyutool runner")
        parser.add_argument(
            "--tyutool", default="tyutool_cli", help="Path to tyutool_cli"
        )
        parser.add_argument(
            "--t5ai-provision",
            action="store_true",
            help="Format T5AI PFS (destroys stored apps/data); requires --resources",
        )
        parser.add_argument(
            "--t5ai-cp",
            action="store_true",
            help="Also flash the validated CP artifacts produced by t5ai_cp",
        )

    @classmethod
    def do_create(cls, cfg, args):
        return cls(
            cfg,
            tty=getattr(args, "tyutool_tty", None),
            tool=getattr(args, "tyutool", "tyutool_cli"),
            provision=getattr(args, "t5ai_provision", False),
            cp=getattr(args, "t5ai_cp", False),
        )

    def do_run(self, command):
        if not self.tty:
            raise RunnerError("Port not specified, use --tyutool-tty")
        if not self.cfg.bin_file:
            raise RunnerError("T5AI flashing requires firmware")
        if self.provision and not self.cfg.resources_file:
            raise RunnerError("--t5ai-provision requires --resources")

        raw = Path(self.cfg.bin_file)
        protected = raw.with_name("pebbleos.t5ai-crc.bin")
        # Validate every selected input before issuing any device command.
        try:
            firmware, ap_image = validate_firmware(raw, protected)
            print(f"Phone firmware version: {validate_current_version(firmware)}")
            ap_output = protected
            if self.cfg.resources_file:
                ap_image = build_pebble_flash_image(
                    firmware, Path(self.cfg.resources_file).read_bytes()
                )
                ap_output = raw.with_name("pebbleos.t5ai-flash.bin")
            writes = [("AP", PEBBLE_AP_PHYSICAL_OFFSET, ap_image, ap_output)]
            if self.cp:
                cp_output = raw.with_name("pebbleos.t5ai-cp-crc.bin")
                _, cp_image = validate_cp(
                    raw.with_name("pebbleos.t5ai-cp.bin"), cp_output
                )
                from tools.unhygienix_cp import validate_manifest

                validate_manifest(raw.parent)
                writes.insert(
                    0, ("CP", PRIMARY_CP_APP_PHYSICAL_OFFSET, cp_image, cp_output)
                )
            if self.provision:
                writes.append(
                    (
                        "PFS",
                        PEBBLE_FILESYSTEM_PHYSICAL_OFFSET,
                        build_empty_pfs(),
                        raw.with_name("pebbleos.t5ai-pfs.bin"),
                    )
                )
        except (ValueError, OSError) as error:
            raise RunnerError(str(error)) from error

        for name, start, image, output in writes:
            if not self.cfg.dry_run:
                output.write_bytes(image)
            end = start + len(image)
            cmd = self.quote(
                self.tool,
                "write",
                "-d",
                "t5ai",
                "-p",
                self.tty,
                "-s",
                hex(start),
                "--end",
                hex(end),
                "-f",
                output,
                "--plain",
            )
            if self.call(cmd):
                raise RunnerError(f"tyutool failed to flash the T5AI {name} image")
