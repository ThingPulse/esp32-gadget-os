#!/usr/bin/env python3
"""Summarize real BENCH records captured from the Pro's USB serial output."""
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from statistics import median


def summarize(log):
    groups = defaultdict(list)
    seen = set()
    for line in log.splitlines():
        # Monitor timestamps may precede a record.
        start = line.find('BENCH,')
        if start < 0:
            continue
        row = next(csv.reader([line[start:]]))
        if len(row) != 12:
            raise ValueError(f'Malformed BENCH record: {line}')
        _, profile, scene, repeat, frames, *values = row
        identity = (profile, scene, int(repeat))
        if identity in seen:
            raise ValueError('Repeated run found: use one BENCH_META … BENCH_DONE capture per file')
        seen.add(identity)
        metrics = list(map(float, values))
        if not all(map(math.isfinite, metrics)) or int(frames) != 90 or min(metrics) < 0 or metrics[0] <= 0:
            raise ValueError(f'Invalid measurement: {line}')
        groups[profile, scene].append(metrics)
    if not groups:
        raise ValueError('No benchmark measurements found')
    lines = ['| Scene | Profile | Runs | Mean refresh ms | P95 ms | Transfer ms | Flushes | Pixels | Time change |',
             '|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for (profile, scene), samples in sorted(groups.items(), key=lambda x: (x[0][1], x[0][0])):
        mean, _, p95, _, io, flushes, pixels = map(median, zip(*samples))
        baseline = groups.get(('baseline-8-psram', scene))
        change = f'{(mean / median(s[0] for s in baseline) - 1) * 100:+.1f}%' if baseline else '—'
        lines.append(f'| {scene} | {profile} | {len(samples)} | {mean / 1000:.3f} | {p95 / 1000:.3f} | {io / 1000:.3f} | {flushes:.1f} | {pixels:.0f} | {change} |')
    complete = 'BENCH_DONE' in log and len(groups) == 15 and all(len(g) == 3 for g in groups.values())
    lines += ['', 'Median of repeated runs; negative time change means faster than the baseline.',
              'Refresh time is measured work, not the LCD scan rate or application FPS.']
    if not complete:
        lines.append('INCOMPLETE RUN: missing profiles, repetitions, or BENCH_DONE; do not treat this as a complete comparison.')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        print(summarize(args.log.read_text()))
    except ValueError as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
