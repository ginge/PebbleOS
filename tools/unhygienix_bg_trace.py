#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarize KernelBG state segments and resolve addresses using the captured ELF."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess

STATES = {0: 'ready', 1: 'running', 2: 'blocked', 3: 'suspended', 4: 'dead'}
HEX = {'cb', 'waitq', 'object', 'caller', 'owner_lr'}


def summarize(text, elf=None, addr2line='arm-none-eabi-addr2line'):
    segments = {}
    windows = []
    loss = 0
    boots = 0
    for line in text.splitlines():
        if 'T5BG start ' in line:
            boots += 1
        if 'T5BG ' not in line:
            continue
        values = {k: int(v, 16 if k in HEX else 10)
                  for k, v in re.findall(r'(\w+)=([0-9a-fA-F]+)(?=\s|\x1b|$)', line)}
        if 'segment' in values:
            segments.setdefault((boots, values['segment']), {'boot': boots}).update(values)
        if 'ready_ms' in values:
            windows.append({'boot': boots, **values})
        if 'picks_bg' in values and windows:
            windows[-1]['scheduler_picks'] = values
        if 'task' in values and windows:
            name = re.search(r'name=(.*?) cpu_ticks=', line)
            windows[-1].setdefault('tasks', []).append({**values, 'name': name[1] if name else ''})
        if 'owner_lr' in values and 'segment' not in values and windows:
            windows[-1]['operation'] = values
        loss = max(loss, values.get('lost', 0))
    records = list(segments.values())
    address_rows = records + windows + [w['operation'] for w in windows if 'operation' in w]
    addresses = sorted({v for row in address_rows for k, v in row.items()
                        if k in {'cb', 'caller', 'owner_lr'} and v})
    symbols = {}
    if elf and addresses:
        result = subprocess.run([addr2line, '-f', '-C', '-e', str(elf),
                                 *[hex(a & ~1) for a in addresses]],
                                check=True, capture_output=True, text=True).stdout.splitlines()
        symbols = {f'{a:08x}': {'function': result[i*2], 'location': result[i*2+1]}
                   for i, a in enumerate(addresses)}
    totals = Counter()
    for row in records:
        if 'state' in row and 'ms' in row:
            totals[STATES.get(row['state'], str(row['state']))] += row['ms']
    task_cpu = Counter()
    for window in windows:
        for task in window.get('tasks', []):
            task_cpu[f"{task['task']}:{task['name']}"] += task['cpu_ticks']
    return {'max_observed_ready_age_ms': max((w.get('age_ms', 0) for w in windows if w.get('state') == 0), default=0),
            'task_cpu_ticks_in_printed_windows': dict(task_cpu.most_common()),
            'boots_with_trace_start': boots, 'lost_records_max_per_boot': loss,
            'completed_segments_ms': dict(totals), 'segments': records,
            'windows': windows, 'symbols': symbols,
            'limits': ['Segments retain intervals >=100 ms; shorter intervals remain in window totals.',
                       'Window totals include normal idle poll waits and cover the time since the previous sample.',
                       'Running means scheduled thread elapsed time, including interrupts, not exclusive CPU cycles.',
                       'Compiler hooks perturb timing; UART loss can omit records independently of ring loss.',
                       'State/callback boundaries are observed at scheduler hooks and wrapped operations.',
                       'Use only the exact ELF and dictionary saved with this capture.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--addr2line', default='arm-none-eabi-addr2line')
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(errors='replace'), args.elf, args.addr2line), indent=2))


if __name__ == '__main__':
    main()
