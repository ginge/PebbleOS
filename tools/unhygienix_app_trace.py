#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Barry Carter <barry.carter@gmail.com>
# SPDX-License-Identifier: Apache-2.0
"""Summarize sampled App PCs and long event processing spans using the captured ELF."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess


def summarize(text, elf=None, addr2line="arm-none-eabi-addr2line"):
    windows, spans = [], []
    counts = Counter()
    for line in text.splitlines():
        if "T5APP " not in line:
            continue
        row = {k: int(v, 16 if k in {"pc", "lr", "cb"} else 10)
               for k, v in re.findall(r"(\w+)=([0-9a-fA-F]+)(?=\s|\x1b|$)", line)}
        if "long id=" in line:
            spans.append(row)
        elif "samples" in row:
            windows.append(row)
        elif "receive_ms" in row and windows:
            windows[-1].update(row)
        elif "pc" in row:
            counts[row["pc"], row.get("lr", 0), row["event"]] += row["count"]
            if windows:
                windows[-1].setdefault("pcs", []).append(row)
    pcs = [{"pc": pc, "lr": lr, "event": event, "count": count}
           for (pc, lr, event), count in counts.most_common()]
    addresses = sorted({row["pc"] for row in pcs} | {row["lr"] for row in pcs if row["lr"]} |
                       {row["cb"] & ~1 for row in spans + windows if row.get("cb")})
    symbols = {}
    if elf and addresses:
        # Resolve both ends: a sixteen-byte bucket can cross a function boundary.
        lines = subprocess.run([addr2line, "-f", "-C", "-e", str(elf),
                                *[hex(a + delta) for a in addresses for delta in (0, 14)]],
                               check=True, capture_output=True, text=True).stdout.splitlines()
        for i, address in enumerate(addresses):
            symbols[f"{address:08x}"] = {"function": lines[i*4], "location": lines[i*4+1],
                                       "end_function": lines[i*4+2], "end_location": lines[i*4+3]}
    return {"windows": windows, "long_events": spans, "top_pc_buckets": pcs, "symbols": symbols,
            "samples_in_complete_headers": sum(w.get("samples", 0) for w in windows),
            "overflow_in_complete_headers": sum(w.get("overflow", 0) for w in windows),
            "limits": ["Jittered SysTick samples exclude handler execution and masked intervals.",
                       "Only twelve busiest sixteen-byte buckets are printed per five-second window.",
                       "CPU ticks include interrupt time while the App is scheduled.",
                       "Event processing includes upkeep after dispatch, before the next queue receive.",
                       "Receive duration includes queue waiting and any scheduling delay.",
                       "Event 254 means queue receive; 255 means no event observed yet.",
                       "UART loss may omit headers or rows independently of buffer overflow.",
                       "RAM app PCs require the matching watchface binary for symbolication."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--addr2line", default="arm-none-eabi-addr2line")
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(errors="replace"), args.elf, args.addr2line), indent=2))


if __name__ == "__main__":
    main()
