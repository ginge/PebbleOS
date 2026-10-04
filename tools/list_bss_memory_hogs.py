# SPDX-FileCopyrightText: 2024 Google LLC
# SPDX-License-Identifier: Apache-2.0

import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description="List the largest BSS symbols")
parser.add_argument("elf", nargs="?", type=Path, default=Path("build/pebbleos.elf"))
args = parser.parse_args()
output = subprocess.check_output(["arm-none-eabi-objdump", "-t", str(args.elf)], text=True)
symbols = []
for line in output.splitlines():
    fields = line.split()
    if len(fields) >= 6 and fields[-3] == ".bss":
        symbols.append((int(fields[-2], 16), fields[-1]))

print("Top 25 BSS Memory Hogs")
print("~~~~~~~~~~~~~~~~~~~~~~")
for size, name in sorted(symbols, reverse=True)[:25]:
    print(f"\t {size} \t {name}")
