#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check production interception boundaries in the final Unhygienix ELF."""

import argparse
import subprocess
from pathlib import Path


def validate_symbols(nm_output: str, config: str, declared: str) -> list[str]:
    enabled = set(config.splitlines())
    symbols = {
        fields[-1].split(".", 1)[0]
        for line in nm_output.splitlines()
        if len(fields := line.split()) >= 3 and fields[-2].upper() != "U"
    }
    wrappers = {
        name.removeprefix("__wrap_") for name in symbols if name.startswith("__wrap_")
    }
    errors = [
        f"undeclared linker wrapper: {name}"
        for name in sorted(wrappers - set(declared.split()))
    ]
    required = set()
    if "CONFIG_CONSOLE=y" in enabled:
        required.update(("__wrap_pbl_console_lock", "__wrap_pbl_console_unlock"))
    if "CONFIG_T5AI_TOUCH_QUEUE_COALESCE=y" in enabled:
        required.update(
            "__wrap_" + name
            for name in (
                "event_put",
                "event_take_timeout",
                "pbl_msgq_put",
                "pbl_msgq_get",
                "recognizer_manager_handle_touch_event",
            )
        )
        required.add("t5ai_touch_sample_ticks")
    if "CONFIG_T5AI_SD_PFS=y" in enabled:
        required.update(("__wrap_pfs_init", "ftl_read", "ftl_write"))
    errors.extend(
        f"missing production boundary: {name}" for name in sorted(required - symbols)
    )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--wraps", type=Path, required=True)
    parser.add_argument("--nm", required=True)
    args = parser.parse_args()
    try:
        symbols = subprocess.check_output(
            [args.nm, "--defined-only", str(args.elf)], text=True
        )
        errors = validate_symbols(
            symbols, args.config.read_text(), args.wraps.read_text()
        )
        if errors:
            raise ValueError("unsafe T5AI build integration:\n  " + "\n  ".join(errors))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
