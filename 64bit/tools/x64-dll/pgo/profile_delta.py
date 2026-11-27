"""Ticket 4801 (epic 0504 P1): the per-function difference of two perf/sampler reports of the same game window (cycles,
self and inclusive), for the "profile before/after" line of a PERFORMANCE.md report.

    python -I tools/x64-dll/pgo/profile_delta.py A/report.txt B/report.txt [--top 25] [--out delta.txt]

Reads the report's "Top N functions by SELF cycles" and "by INCLUSIVE cycles" tables (the cycles column, in M).
Prints the total cycles of each, then the functions whose cycles moved most (B - A, in G cycles and as a share of A's
total). A function missing from one table counts as 0 there (it was below the top N), so look at the larger moves only.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

ROW = re.compile(r'^\s+([\d.]+)%\s+([\d.]+)M\s+(.+?)\s*$')
TOTAL = re.compile(r'cycles in them ([\d.]+) G')


def read(path: Path) -> dict:
    out = {'self': {}, 'incl': {}, 'total_g': None}
    table = None
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        m = TOTAL.search(line)
        if m:
            out['total_g'] = float(m.group(1))
        if 'functions by SELF cycles' in line:
            table = 'self'
            continue
        if 'functions by INCLUSIVE cycles' in line:
            table = 'incl'
            continue
        if table and not line.strip():
            table = None
            continue
        m = ROW.match(line) if table else None
        if m:
            out[table][m.group(3)] = float(m.group(2)) / 1000.0
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('a', type=Path)
    ap.add_argument('b', type=Path)
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--out', type=Path)
    x = ap.parse_args()
    a, b = read(x.a), read(x.b)
    lines = [f'A {x.a}: {a["total_g"]} G cycles sampled', f'B {x.b}: {b["total_g"]} G cycles sampled',
             f'B - A total: {b["total_g"] - a["total_g"]:+.1f} G ({100.0 * (b["total_g"] - a["total_g"]) / a["total_g"]:+.1f} %)']
    for t, title in (('self', 'SELF'), ('incl', 'INCLUSIVE')):
        names = set(a[t]) | set(b[t])
        rows = sorted(((b[t].get(n, 0.0) - a[t].get(n, 0.0), n) for n in names), key=lambda r: r[0])
        lines.append(f'\n{title}: largest moves (G cycles, B - A; share of A\'s total), A -> B')
        for d, n in rows[:x.top] + [r for r in rows[-5:] if r[0] > 0]:
            lines.append(f'  {d:+8.2f} G  {100.0 * d / a["total_g"]:+6.2f} %   {a[t].get(n, 0.0):8.2f} -> '
                         f'{b[t].get(n, 0.0):8.2f}  {n}')
    text = '\n'.join(lines) + '\n'
    print(text)
    if x.out:
        x.out.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    main()
