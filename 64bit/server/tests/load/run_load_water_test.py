"""Issue 0452: after a load, the server's direct mode has the plots' water flags set (live; one turn of a real save).

    python -I server/tests/load/run_load_water_test.py --bin <layout>/bin --save SAVE.Civ5Save --turn T --run DIR
        [--expect ADJ LAKE FRESH] [--db DB] [--game-dir DIR] [--loc-db DB]

The EXE recalculates the landmasses once in its body reader, after the 64 player reads (research/03-dll-engine-boundary/
server-host.md section 3, "Load"); CvMap::recalculateLandmasses starts with updateWaterFlags on every plot. The plots'
adjacent-to-land, lake and fresh-water flags are not saved, so a host that skips the call plays with them all false
(ticket 4655). This test loads SAVE with server/run_server.py (--load, --turns T+1, AI logging off) and runs
water_counts.lua at the arm, which reads the cached flags without recomputing them, before any turn is played. It
passes when every count is above zero and, with --expect, equal to the counts the windowed client reads on the same
save at the same point (TS0438-M-T150: 712 15 392; TS0438-H-T300: 1907 62 796).

The run folder DIR (outside the repository) is replaced. Starts go through run_server.py's launch gate.
Exit 0 pass; 1 a count is zero or differs from --expect (a server without the call: 0 0 0); 2 the run failed or wrote
no count line.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PROBE = HERE / 'water_counts.lua'
LINE = re.compile(r'0452W\|cached\|turn (\d+)\|plots (\d+)\|water (\d+)\|adjacentToLand (\d+)\|lake (\d+)\|freshwater (\d+)')


def find_line(run: Path) -> re.Match | None:
    """The probe's line, from any text file the run wrote (server.log, server-stdout.txt, the Lua logs)."""
    for p in sorted(run.rglob('*')):
        if p.is_file() and p.suffix.lower() in ('.log', '.txt') and p.stat().st_size < 64 * 1024 * 1024:
            m = LINE.search(p.read_text(encoding='utf-8', errors='replace'))
            if m:
                return m
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bin', type=Path, required=True, help='server layout bin folder (server.exe and the x64 DLL)')
    ap.add_argument('--save', type=Path, required=True)
    ap.add_argument('--turn', type=int, required=True, help="the save's game turn")
    ap.add_argument('--run', type=Path, required=True, help='run folder, outside the repository (replaced)')
    ap.add_argument('--expect', type=int, nargs=3, metavar=('ADJ', 'LAKE', 'FRESH'))
    ap.add_argument('--db', type=Path, help='run_server.py --db')
    ap.add_argument('--game-dir', type=Path, help='run_server.py --game-dir')
    ap.add_argument('--loc-db', help='run_server.py --loc-db')
    ap.add_argument('--timeout', type=int, default=1800)
    a = ap.parse_args()
    run = a.run.resolve()
    if REPO in run.parents or run == REPO:
        print('run_load_water_test: FAILED: the run folder must be outside the repository')
        return 2
    if run.exists():
        shutil.rmtree(run)
    cmd = [sys.executable, '-I', str(REPO / 'server' / 'run_server.py'), '--load', str(a.save), '--turns', str(a.turn + 1),
           '--run', str(run), '--bin', str(a.bin), '--dll-log-flags', '0', '--timeout', str(a.timeout),
           '--lua-probe', str(PROBE)]
    for flag, v in (('--db', a.db), ('--game-dir', a.game_dir), ('--loc-db', a.loc_db)):
        if v:
            cmd += [flag, str(v)]
    p = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=a.timeout + 300)
    tail = (p.stdout + p.stderr).strip().splitlines()[-5:]
    if p.returncode != 0:
        print('run_load_water_test: FAILED: run_server.py exit %d' % p.returncode)
        print('\n'.join(tail))
        return 2
    m = find_line(run)
    if not m:
        print('run_load_water_test: FAILED: no 0452W line in %s (the probe did not run)' % run)
        return 2
    turn, plots, water, adj, lake, fresh = (int(x) for x in m.groups())
    print('run_load_water_test: turn %d, plots %d, water %d; cached adjacent-to-land %d, lake %d, fresh water %d'
          % (turn, plots, water, adj, lake, fresh))
    if turn != a.turn:
        print('run_load_water_test: FAILED: the probe ran at turn %d, not at the save\'s turn %d' % (turn, a.turn))
        return 2
    bad = []
    if min(adj, lake, fresh) <= 0:
        bad.append('a count is zero: the water flags were not computed after the load')
    if a.expect and (adj, lake, fresh) != tuple(a.expect):
        bad.append('expected %d %d %d' % tuple(a.expect))
    if bad:
        print('run_load_water_test: FAILED: ' + '; '.join(bad))
        return 1
    print('run_load_water_test: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
