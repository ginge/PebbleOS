#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0

"""Capture and dehash Pebble logs from a T5AI runtime UART."""

from __future__ import annotations

import argparse
import glob
import os
import sys
import time
from datetime import datetime

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLS_LIBS_DIR = os.path.join(TOOLS_DIR, "libs")
for path in (TOOLS_DIR, TOOLS_LIBS_DIR):
    if path not in sys.path:
        sys.path.insert(0, path)

from pkgutil import extend_path
import pebble

pebble.__path__ = extend_path(pebble.__path__, pebble.__name__)

from log_hashing.logdehash import LogDehash
from pebble import commander, pulse2


DEFAULT_TTY_GLOB = "/dev/serial/by-id/*USB_Dual_Serial*-if02"
DEFAULT_BAUD = 460800


def discover_runtime_tty(pattern: str = DEFAULT_TTY_GLOB) -> str | None:
    """Return the sole T5AI runtime UART matching ``pattern``."""
    matches = sorted(glob.glob(pattern))
    if not matches:
        return None
    if len(matches) != 1:
        raise RuntimeError(
            "multiple runtime UARTs matched; select one with --tty: "
            + ", ".join(matches)
        )
    return matches[0]


def wait_for_runtime_tty(
    explicit_tty: str | None,
    pattern: str,
    timeout_seconds: float,
    poll_seconds: float = 0.25,
) -> str:
    """Wait for an explicit or auto-discovered runtime UART."""
    deadline = None if timeout_seconds == 0 else time.monotonic() + timeout_seconds
    while True:
        tty = explicit_tty if explicit_tty and os.path.exists(explicit_tty) else None
        if tty is None and explicit_tty is None:
            tty = discover_runtime_tty(pattern)
        if tty is not None:
            return tty
        if deadline is not None and time.monotonic() >= deadline:
            target = explicit_tty or pattern
            raise TimeoutError(f"runtime UART did not appear: {target}")
        time.sleep(poll_seconds)


def default_output_path() -> str:
    stamp = datetime.now().astimezone().strftime("%Y%m%d-%H%M%S")
    return os.path.join("build", "traces", f"t5ai-{stamp}.log")


def _write_line(output, line: str) -> None:
    print(line, flush=True)
    output.write(line + "\n")
    output.flush()


def _run_prompt_command(
    interface, command: str, output, timeout_seconds: float
) -> None:
    link = interface.get_link(timeout=timeout_seconds)
    if link is None:
        raise RuntimeError("PULSE reliable link did not come up")
    prompt = commander.apps.Prompt(link)
    try:
        _write_line(output, f"--- command: {command} ---")
        for line in prompt.command_and_response(command):
            _write_line(output, line)
    finally:
        prompt.close()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-t", "--tty", help="runtime UART path; auto-detected by default"
    )
    parser.add_argument(
        "--tty-glob",
        default=DEFAULT_TTY_GLOB,
        help="auto-detection glob used when --tty is omitted",
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=60,
        metavar="SECONDS",
        help="seconds to wait for the UART; 0 waits forever (default: 60)",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0,
        metavar="SECONDS",
        help="capture duration; 0 runs until Ctrl-C (default: 0)",
    )
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--dict",
        dest="dict_path",
        default="build/src/fw/loghash_dict.json",
        help="firmware log-hash dictionary",
    )
    parser.add_argument("-o", "--output", default=None, help="trace output path")
    parser.add_argument(
        "--command",
        action="append",
        default=[],
        help="read-only console command to run after connecting; repeatable",
    )
    parser.add_argument(
        "--command-timeout",
        type=float,
        default=5,
        metavar="SECONDS",
        help="seconds to wait for the bidirectional PULSE link (default: 5)",
    )
    parser.add_argument(
        "--reconnect-idle",
        type=float,
        default=10,
        metavar="SECONDS",
        help="reopen a quiet PULSE session after this interval; 0 disables (default: 10)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if (
        args.wait < 0
        or args.duration < 0
        or args.command_timeout <= 0
        or args.reconnect_idle < 0
        or args.baud <= 0
    ):
        raise SystemExit(
            "--wait/--duration/--reconnect-idle must be non-negative; "
            "--command-timeout/--baud must be positive"
        )
    if not os.path.isfile(args.dict_path):
        raise SystemExit(f"log dictionary not found: {args.dict_path}")

    output_path = args.output or default_output_path()
    output_dir = os.path.dirname(os.path.abspath(output_path))
    os.makedirs(output_dir, exist_ok=True)

    try:
        tty = wait_for_runtime_tty(args.tty, args.tty_glob, args.wait)
    except (RuntimeError, TimeoutError) as error:
        raise SystemExit(str(error)) from error

    # pulse2 defaults to the 1 Mbaud used by other Pebble boards. Unhygienix's CP
    # runtime UART is pinned to 460800 baud.
    pulse2.link.DBGSERIAL_PORT_SETTINGS["baudrate"] = args.baud
    dehasher = LogDehash(args.dict_path, color=False, monitor_dict_file=False)
    deadline = None if args.duration == 0 else time.monotonic() + args.duration

    with open(output_path, "a", encoding="utf-8") as output:
        _write_line(output, f"--- waiting complete: {tty} at {args.baud} baud ---")
        first_session = True
        while deadline is None or time.monotonic() < deadline:
            interface = None
            logging_app = None
            try:
                tty = wait_for_runtime_tty(args.tty, args.tty_glob, args.wait)
                interface = pulse2.Interface.open_dbgserial(url=tty)
                logging_app = commander.apps.StreamingLogs(interface)
                marker = "TRACE READY" if first_session else "TRACE RECONNECTED"
                _write_line(output, f"--- {marker}: {output_path} ---")
                first_session = False

                for command in args.command:
                    try:
                        _run_prompt_command(
                            interface, command, output, args.command_timeout
                        )
                    except (
                        RuntimeError,
                        commander.exceptions.CommandTimedOut,
                    ) as error:
                        _write_line(
                            output, f"--- command unavailable: {command}: {error} ---"
                        )

                last_message = time.monotonic()
                while deadline is None or time.monotonic() < deadline:
                    try:
                        message = logging_app.receive(block=True, timeout=0.5)
                    except pulse2.exceptions.ReceiveQueueEmpty:
                        if (
                            args.reconnect_idle
                            and time.monotonic() - last_message >= args.reconnect_idle
                        ):
                            _write_line(
                                output, "--- trace idle: reopening PULSE session ---"
                            )
                            break
                        continue
                    last_message = time.monotonic()
                    line = dehasher.commander_format_line(dehasher.dehash(message))
                    _write_line(output, f"[host_mono_ns={time.monotonic_ns()}] {line}")
            except KeyboardInterrupt:
                _write_line(output, "--- trace stopped by user ---")
                return 0
            except pulse2.exceptions.SocketClosed:
                _write_line(
                    output, "--- serial connection closed: waiting to reconnect ---"
                )
            finally:
                if logging_app is not None:
                    logging_app.close()
                if interface is not None:
                    interface.close()

            if deadline is not None and time.monotonic() >= deadline:
                break
            time.sleep(0.25)

    return 0


if __name__ == "__main__":
    sys.exit(main())
