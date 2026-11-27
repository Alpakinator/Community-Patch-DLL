"""Live check of bug 0100: the server and the 32-bit host c5h load the same map script file for one spec, and the map is
the same at turn 0.

  python tools/drift/run_pair.py --spec server/tests/specs/communitu79a-large-5005-6006.json --turns 1 --out OUT
         --srv-bin BIN --tag large-communitu
  python server/tests/check_map_script_pair.py --x64-run OUT/large-communitu-x64 --x86-run OUT/large-communitu-x86

Checks, each printed as PASS or FAIL:
  1. the server's server.log names the file its LoadFile of the map script read (`LoadFile("...") -> PATH`);
  2. that file is not the base game's copy (`Assets\\Maps\\<name>`) when a package holds the same name (--expect-package,
     default on: the spec's Communitu_79a.lua is in VP's package);
  3. c5h's host-report.json names a file of the same name, and both are the same path relative to the game folder;
  4. MapUnitsState at turn 0 (every starting unit and its plot) is equal in the two stats.db and not empty;
  5. MapPlotsState of turns 0-1 is equal (it logs changed plots only; turn 0 alone is one sentinel row for any map).
Exit 0 when every check passes, 1 otherwise. Standard-library Python.
"""
import argparse
import json
import re
import sqlite3
import sys
from pathlib import Path

GAME_MARK = "sid meier's civilization v\\"


def rel_to_game(p):
    """The path below the game folder, lower case with backslashes; the whole path if no game folder is in it."""
    s = p.replace('/', '\\').lower()
    i = s.rfind(GAME_MARK)
    return s[i + len(GAME_MARK):] if i >= 0 else s


def server_loadfile(run):
    log = run / 'server.log'
    if not log.is_file():
        return None, f'no {log}'
    hits = []
    for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
        m = re.search(r'LoadFile\("([^"]*)"\) -> (.+)$', line)
        if m:
            hits.append((m.group(1), m.group(2).strip()))
    scripts = [h for h in hits if '\\maps\\' in h[0].replace('/', '\\').lower() or 'mapscripts' in h[0].lower()]
    if not scripts:
        nf = [line for line in log.read_text(encoding='utf-8', errors='replace').splitlines() if 'LoadFile(' in line]
        return None, 'no LoadFile of a map script in server.log' + (f' (lines: {nf[:3]})' if nf else '')
    return scripts[0], None


def c5h_script_path(run, name):
    rep = run / 'host-report.json'
    if not rep.is_file():
        return None, f'no {rep}'
    found = []

    def walk(o):
        if isinstance(o, dict):
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
        elif isinstance(o, str) and o.replace('/', '\\').lower().endswith('\\' + name.lower()):
            found.append(o)
    walk(json.loads(rep.read_text(encoding='utf-8', errors='replace')))
    if not found:
        return None, f'host-report.json names no path ending in {name}'
    return sorted(set(found)), None


def table_rows(db, table, max_turn):
    """Rows of `table` with Turn <= max_turn, sorted. MapPlotsState logs only changed plots (turn 0 has a single
    sentinel row, the same for any map), so the map is compared through MapUnitsState at turn 0 (every starting
    unit with its plot: the start positions the map script and AssignStartingPlots chose) and MapPlotsState of turn 1."""
    if not db.is_file():
        return None, f'no {db}'
    con = sqlite3.connect(db.resolve().as_uri() + '?mode=ro', uri=True)
    try:
        cols = [r[1] for r in con.execute(f'pragma table_info({table})')]
        if not cols:
            return None, f'{db}: no {table} table'
        order = ', '.join(f'"{c}"' for c in cols)
        rows = con.execute(f'select * from {table} where Turn <= ? order by {order}', (max_turn,)).fetchall()
        return (cols, rows), None
    finally:
        con.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--x64-run', type=Path, required=True)
    ap.add_argument('--x86-run', type=Path, required=True)
    ap.add_argument('--no-expect-package', action='store_true', help='skip check 2 (a script that exists only once)')
    a = ap.parse_args()
    fails = 0

    def report(ok, what):
        nonlocal fails
        print(('PASS ' if ok else 'FAIL ') + what)
        fails += 0 if ok else 1

    hit, why = server_loadfile(a.x64_run)
    report(hit is not None, f'1 server LoadFile: {hit[0]!r} -> {hit[1]}' if hit else f'1 server LoadFile: {why}')
    srv_rel = rel_to_game(hit[1]) if hit else None
    name = hit[0].replace('/', '\\').rsplit('\\', 1)[-1] if hit else None
    if not a.no_expect_package:
        ok = srv_rel is not None and not srv_rel.startswith('assets\\maps\\') and srv_rel.startswith('assets\\dlc\\')
        report(ok, f'2 server file is a package copy, not the base game\'s: {srv_rel}')
    if name:
        paths, why = c5h_script_path(a.x86_run, name)
        if paths is None:
            report(False, f'3 c5h file: {why}')
        else:
            rels = sorted({rel_to_game(p) for p in paths})
            report(rels == [srv_rel], f'3 same file in both hosts: server {srv_rel}; c5h {rels}')
    else:
        report(False, '3 same file in both hosts: no server file to compare')
    for num, table, max_turn in (('4', 'MapUnitsState', 0), ('5', 'MapPlotsState', 1)):
        what = f'{num} {table} turns 0-{max_turn}'
        p64, why64 = table_rows(a.x64_run / 'cache' / 'stats.db', table, max_turn)
        p86, why86 = table_rows(a.x86_run / 'cache' / 'stats.db', table, max_turn)
        if p64 is None or p86 is None:
            report(False, f'{what}: {why64 or ""} {why86 or ""}'.strip())
            continue
        same = p64 == p86 and len(p64[1]) > 1
        first = next((i for i, (r1, r2) in enumerate(zip(p64[1], p86[1])) if r1 != r2), None)
        report(same, f'{what}: server {len(p64[1])} rows, c5h {len(p86[1])} rows, '
                     + ('equal' if same else f'different or empty (columns equal: {p64[0] == p86[0]}; first differing sorted row {first})'))
    print(f'check_map_script_pair: {"OK" if fails == 0 else "FAILED"} ({fails} failed)')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
