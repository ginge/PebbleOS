#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Summarize complete T5 display timing windows from a decoded UART capture."""

from __future__ import annotations

import argparse
import json
import re
from bisect import bisect_right
from itertools import pairwise
from pathlib import Path

RTC_HZ = 32000
RECORD = re.compile(r"\b(T5FRAME|T5DRAW|T5PHASE|T5XFER|T5PREP|T5DMA)\s+(.*)")
FIELDS = re.compile(r"([a-z0-9_]+)=(-?\d+)")
REQUIRED = {
    "T5FRAME": {"seq", "frames", "intervals", "span_lo", "span_hi", "min", "max"},
    "T5DRAW": {
        "seq",
        "update_us",
        "update_max_us",
        "over30",
        "dma_fail",
        "invalid",
        "hz",
    },
    "T5PHASE": {
        "seq",
        "render_n",
        "render_us",
        "comp_n",
        "comp_us",
        "font_n",
        "font_us",
    },
}


def _read_windows(lines, required):
    """Keep log loss and reboot boundaries visible; never bridge missing records."""
    windows, rejected = [], []
    pending = {}
    session, previous = 1, None

    def finish():
        if not pending:
            return
        seq = pending["T5FRAME"]["seq"]
        if set(pending) != set(required):
            rejected.append(
                {"session": session, "seq": seq, "reason": "incomplete records"}
            )
            return
        row = {"session": session}
        for kind, fields in pending.items():
            if not required[kind] <= fields.keys():
                rejected.append(
                    {"session": session, "seq": seq, "reason": "missing fields"}
                )
                return
            row.update(fields)
        if row["invalid"] or not row["hz"] or row["frames"] != 16:
            rejected.append(
                {"session": session, "seq": seq, "reason": "invalid timing window"}
            )
            return
        row["span"] = row["span_lo"] | (row["span_hi"] << 32)
        windows.append(row)

    for line in lines:
        match = RECORD.search(line)
        if not match:
            continue
        kind = match[1]
        if kind not in required:
            continue
        # Some legacy dehashers print uint32 arguments as signed integers.
        fields = {
            key: int(value) & 0xFFFFFFFF for key, value in FIELDS.findall(match[2])
        }
        if "seq" not in fields:
            continue
        if kind == "T5FRAME":
            finish()
            if previous is not None and fields["seq"] <= previous:
                session += 1
            previous = fields["seq"]
            pending = {kind: fields}
        elif pending and fields["seq"] == pending["T5FRAME"]["seq"]:
            pending[kind] = fields
    finish()
    return windows, rejected


def read_windows(lines):
    return _read_windows(lines, REQUIRED)


def read_transfer_windows(lines):
    return _read_windows(
        lines,
        {
            **REQUIRED,
            "T5XFER": {
                "seq",
                "rows",
                "panel_rows",
                "bytes",
                "dma_n",
                "dma_frames",
                "cpu_frames",
            },
            "T5PREP": {
                "seq",
                "convert_us",
                "copy_us",
                "clean_us",
                "next_us",
                "command_us",
                "cpu_us",
            },
            "T5DMA": {
                "seq",
                "wait_us",
                "kick_us",
                "observed_us",
                "wait_max_us",
                "lock_us",
            },
        },
    )


def summarize_transfer(windows):
    result = summarize(windows)
    frames = result["frames"]
    result.update(
        bytes_per_frame=sum(w["bytes"] for w in windows) / frames,
        logical_rows_per_frame=sum(w["rows"] for w in windows) / frames,
        panel_rows_per_frame=sum(w["panel_rows"] for w in windows) / frames,
        dma_submissions_per_frame=sum(w["dma_n"] for w in windows) / frames,
        dma_frames=sum(w["dma_frames"] for w in windows),
        cpu_frames=sum(w["cpu_frames"] for w in windows),
        stages_per_frame_ms={
            key: sum(w[key] for w in windows) / frames / 1000
            for key in (
                "convert_us",
                "copy_us",
                "clean_us",
                "next_us",
                "command_us",
                "cpu_us",
                "wait_us",
                "kick_us",
                "lock_us",
            )
        },
        observed_dma_per_frame_ms=sum(w["observed_us"] for w in windows)
        / frames
        / 1000,
        max_poll_wait_ms=max(w["wait_max_us"] for w in windows) / 1000,
        transfer_notes=[
            "Observed DMA spans run from submission to CPU observation; they overlap preparation and are upper bounds, not wire time.",
            "Stage times include preemption; cycle conversion assumes the window clock is constant.",
            "DMA frame counts describe selected mode; submissions prove actual DMA use, and failures remain separately counted.",
        ],
    )
    return result


def summarize(windows):
    frames = sum(w["frames"] for w in windows)
    intervals = sum(w["intervals"] for w in windows)
    span = sum(w["span"] for w in windows)
    result = {
        "windows": len(windows),
        "frames": frames,
        "intervals": intervals,
        "measured_interval_seconds": span / RTC_HZ,
        "fps": intervals * RTC_HZ / span if span else None,
        "interval_mean_ms": span * 1000 / RTC_HZ / intervals if intervals else None,
        "interval_min_ms": min(w["min"] for w in windows) * 1000 / RTC_HZ,
        "interval_max_ms": max(w["max"] for w in windows) * 1000 / RTC_HZ,
        "intervals_over_30fps_budget": sum(w["over30"] for w in windows),
        "transfer_mean_ms": sum(w["update_us"] for w in windows) / frames / 1000,
        "transfer_max_ms": max(w["update_max_us"] for w in windows) / 1000,
        "dma_failures_at_end": windows[-1]["dma_fail"],
        "clock_hz": sorted({w["hz"] for w in windows}),
    }
    for phase in ("render", "comp", "font"):
        count = sum(w[f"{phase}_n"] for w in windows)
        result[f"{phase}_calls"] = count
        result[f"{phase}_mean_ms"] = (
            sum(w[f"{phase}_us"] for w in windows) / count / 1000 if count else None
        )
    return result


def read_cadence(lines):
    """Unwrap per-update timestamps, preserving resets and lost-record boundaries."""
    rows = []
    session, segment, ticks = 1, 1, 0
    previous = None
    for line in lines:
        match = re.search(r"\bT5CADENCE\s+(.*)", line)
        if not match:
            continue
        row = {key: int(value) & 0xFFFFFFFF for key, value in FIELDS.findall(match[1])}
        if not {"frame", "tick", "elapsed", "hz", "dma_fail"} <= row.keys():
            continue
        host = re.search(r"\[host_mono_ns=(\d+)\]", line)
        row["host_ns"] = int(host[1]) if host else None
        row["lost_before"] = 0
        if previous:
            if row["frame"] <= previous["frame"]:
                session += 1
                segment += 1
                ticks = 0
            else:
                ticks += (row["tick"] - previous["tick"]) & 0xFFFFFFFF
                row["lost_before"] = row["frame"] - previous["frame"] - 1
                if row["lost_before"]:
                    segment += 1
        row.update(session=session, segment=segment, seconds=ticks / RTC_HZ)
        rows.append(row)
        previous = row
    return rows


def summarize_cadence(rows):
    """Peak rates require contiguous records; idle periods remain in the timeline."""
    groups = [
        [r for r in rows if r["segment"] == segment]
        for segment in sorted({r["segment"] for r in rows})
    ]
    intervals = [
        b["seconds"] - a["seconds"] for group in groups for a, b in pairwise(group)
    ]
    positive = [dt for dt in intervals if dt > 0]
    result = {
        "received_updates": len(rows),
        "missing_update_records": sum(r["lost_before"] for r in rows),
        "fastest_interval_ms": min(positive) * 1000 if positive else None,
        "peak_single_interval_fps": 1 / min(positive) if positive else None,
        "peak_250ms_fps": None,
        "peak_1s_fps": None,
        "peak_16_interval_fps": None,
        "transfer_max_ms": max(r["elapsed"] for r in rows) * 1000 / RTC_HZ,
        "timeline_250ms": [],
    }
    for width, key in ((0.25, "peak_250ms_fps"), (1.0, "peak_1s_fps")):
        rates = []
        for group in groups:
            times = [r["seconds"] for r in group]
            for i, end in enumerate(times):
                if end - times[0] >= width:
                    rates.append((i + 1 - bisect_right(times, end - width)) / width)
        result[key] = max(rates) if rates else None
    bursts = [
        16 / (group[i]["seconds"] - group[i - 16]["seconds"])
        for group in groups
        for i in range(16, len(group))
        if group[i]["seconds"] > group[i - 16]["seconds"]
    ]
    result["peak_16_interval_fps"] = max(bursts) if bursts else None
    # Only complete bins inside a contiguous segment are included. A missing
    # UART packet must not masquerade as an idle or low-frame-rate interval.
    for group in groups:
        times = [r["seconds"] for r in group]
        start = times[0]
        n = int((times[-1] - start) / 0.25)
        for i in range(n):
            left, right = start + i * 0.25, start + (i + 1) * 0.25
            count = bisect_right(times, right) - bisect_right(times, left)
            result["timeline_250ms"].append({"start_seconds": left, "fps": count * 4})
    first, last = rows[0], rows[-1]
    result["host_crosscheck"] = None
    if first["host_ns"] is not None and last["host_ns"] is not None:
        host_span = (last["host_ns"] - first["host_ns"]) / 1e9
        rtc_span = last["seconds"] - first["seconds"]
        if host_span > 0:
            result["host_crosscheck"] = {
                "host_seconds": host_span,
                "rtc_seconds": rtc_span,
                "rtc_to_host_ratio": rtc_span / host_span,
                "counter_delta": last["frame"] - first["frame"],
                "host_counter_fps": (last["frame"] - first["frame"]) / host_span,
            }
    return result


def read_peaks(lines):
    rows, session, previous = [], 1, None
    for line in lines:
        match = re.search(r"\bT5PEAK\s+(.*)", line)
        if not match:
            continue
        row = {key: int(value) & 0xFFFFFFFF for key, value in FIELDS.findall(match[1])}
        if (
            not {
                "seq",
                "count250",
                "count1000",
                "min_ticks",
                "overflow",
                "local250",
                "local1000",
            }
            <= row.keys()
        ):
            continue
        if previous is not None and row["seq"] <= previous:
            session += 1
        previous = row["seq"]
        row["session"] = session
        rows.append(row)
    return rows


def summarize_peaks(rows):
    return {
        "peak_250ms_fps": max(r["local250"] for r in rows) * 4,
        "peak_1s_fps": max(r["local1000"] for r in rows),
        "since_boot_peak_250ms_fps": max(r["count250"] for r in rows) * 4,
        "since_boot_peak_1s_fps": max(r["count1000"] for r in rows),
        "since_boot_peak_single_interval_fps": RTC_HZ
        / min(r["min_ticks"] for r in rows if r["min_ticks"])
        if any(r["min_ticks"] for r in rows)
        else None,
        "history_overflow": any(r["overflow"] for r in rows),
        "sequences": [r["seq"] for r in rows],
        "missing_peak_windows": sum(b["seq"] - a["seq"] - 1 for a, b in pairwise(rows)),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument(
        "--label", default="unidentified workload", help="operator-supplied face name"
    )
    parser.add_argument(
        "--session", type=int, help="select a session after counter restart"
    )
    parser.add_argument(
        "--first-window", type=int, default=1, help="first sequence to include"
    )
    parser.add_argument("--last-window", type=int, help="last sequence to include")
    parser.add_argument(
        "--transfer",
        action="store_true",
        help="require detailed transfer-stage records",
    )
    args = parser.parse_args(argv)
    if args.first_window < 1 or (
        args.last_window is not None and args.last_window < args.first_window
    ):
        parser.error("window bounds must be positive and ordered")
    lines = args.trace.read_text(errors="replace").splitlines()
    windows, rejected = (read_transfer_windows if args.transfer else read_windows)(
        lines
    )
    cadence = [
        r
        for r in read_cadence(lines)
        if r["frame"] > (args.first_window - 1) * 16
        and (args.last_window is None or r["frame"] <= args.last_window * 16)
        and (args.session is None or r["session"] == args.session)
    ]
    selected = [
        w
        for w in windows
        if w["seq"] >= args.first_window
        and (args.last_window is None or w["seq"] <= args.last_window)
        and (args.session is None or w["session"] == args.session)
    ]
    device_peaks = [
        r
        for r in read_peaks(lines)
        if r["seq"] >= args.first_window
        and (args.last_window is None or r["seq"] <= args.last_window)
        and (args.session is None or r["session"] == args.session)
    ]
    if not selected and not cadence and not device_peaks:
        parser.error("no complete valid profiling windows in the selected range")
    sessions = {}
    for session in sorted({w["session"] for w in selected}):
        rows = [w for w in selected if w["session"] == session]
        sessions[session] = (summarize_transfer if args.transfer else summarize)(rows)
        sessions[session]["sequences"] = [w["seq"] for w in rows]
    peaks = {}
    for session in sorted({r["session"] for r in cadence}):
        anchors = [r for r in cadence if r["session"] == session]
        peaks[session] = {
            "received_anchors": len(anchors),
            "host_crosscheck": summarize_cadence(anchors)["host_crosscheck"],
        }
    device = {
        session: summarize_peaks([r for r in device_peaks if r["session"] == session])
        for session in sorted({r["session"] for r in device_peaks})
    }
    print(
        json.dumps(
            {
                "label": args.label,
                "device_peak_sessions": device,
                "sparse_anchor_sessions": peaks,
                "sessions": sessions,
                "rejected_windows": rejected,
                "notes": [
                    "Label is supplied by the operator, not verified from firmware.",
                    "Device peaks count every driver update before logging; sparse anchors are not a frame timeline.",
                    "Local peaks cover selected windows; since-boot peaks include startup and menus.",
                    "Missing peak windows make local maxima lower bounds; since-boot maxima persist across log loss.",
                    "History overflow invalidates the device rolling-peak measurement.",
                    "250ms/1s peaks count updates in sliding windows with full observed coverage.",
                    "Single-interval peak is a burst bound, not sustained FPS.",
                    "Host timestamps include UART buffering and decode latency.",
                    "Intervals include idle time and logging/scheduling overhead.",
                    "Phase times are elapsed cycle equivalents, including preemption.",
                    "Font time is nested within rendering; do not add it to frame time.",
                    "Incomplete final windows (fewer than 16 updates) are not emitted.",
                ],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
