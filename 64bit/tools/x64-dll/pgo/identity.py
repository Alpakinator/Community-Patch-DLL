"""Ticket 4801 (epic 0504 P1): stats.db identity of DLL arms on the windows of runs.SETS (H 300-305, M 150-160,
S 0-50), headless, one run at a time.

    python -I tools/x64-dll/pgo/identity.py --arm pin=<pin bin>/CvGameCore_Expansion2.dll --arm pgo=<dll> ...
        --template-bin <pin layout>/bin --work DIR --saves DIR --db <0438's statistics-on db> [--sets H,M,S]
        [--reuse NAME:SET=RUNDIR ...]

Each arm's DLL goes into a copy of the pin layout's bin (runs.make_layout); each window is played by every arm
(server/run_server.py, gated); then perf/evidence/0438/idtrace.py compares every pair of the window's stats.db
(tools/d10/statscmp.py). --reuse names a finished run folder (for example a training run of the same window) instead of
playing it again. Exit 0 only when every run passed and every comparison is IDENTICAL; a stop file DIR/STOP ends it
before the next run.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import runs  # noqa: E402

REPO = runs.REPO


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--arm', action='append', required=True, metavar='NAME=DLL')
    ap.add_argument('--template-bin', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--saves', type=Path, required=True)
    ap.add_argument('--db', type=Path, default=None, help="0438's statistics-on database (sets H and M only)")
    ap.add_argument('--release', default=None,
                    help='the VP release the windows run on (default $C5_RELEASE, else deps/dll.lock base; ticket 0774)')
    ap.add_argument('--sets', default=None, help='default by the release (runs.DEFAULT_SETS)')
    ap.add_argument('--reuse', action='append', default=[], metavar='NAME:SET=RUNDIR')
    a = ap.parse_args()
    a.release = a.release or os.environ.get('C5_RELEASE', '').strip() or runs.lock_release()
    a.sets = a.sets or runs.DEFAULT_SETS.get(a.release, 'S')
    a.work.mkdir(parents=True, exist_ok=True)
    logf = open(a.work / 'identity.log', 'a', encoding='utf-8')

    def log(t):
        print(t, flush=True)
        logf.write(t + '\n')
        logf.flush()

    def fail(msg):
        log(f'identity: FAILED: {msg}')
        sys.exit(1)

    arms = dict(x.split('=', 1) for x in a.arm)
    names = [n.strip() for n in a.sets.split(',') if n.strip()]
    if len(arms) < 2 or any(n not in runs.SETS for n in names):
        fail('two or more --arm and --sets from ' + ', '.join(runs.SETS))
    reuse = {}
    for r in a.reuse:
        k, v = r.split('=', 1)
        reuse[tuple(k.split(':', 1))] = Path(v)
    bad = runs.check_release(a.release, names) + runs.check_inputs(a.saves, a.db, names)
    if bad:
        fail('; '.join(bad))
    stop = a.work / 'STOP'
    bins = {}
    for arm, dll in arms.items():
        bins[arm] = runs.make_layout(a.template_bin, Path(dll), a.work / f'layout-{arm}')
        log(f'{time.strftime("%Y-%m-%d %H:%M:%S")} arm {arm}: DLL sha256 {runs.sha256(dll)}')
    results = {'arms': {k: runs.sha256(v) for k, v in arms.items()}, 'runs': [], 'windows': {}}
    rc_all = 0
    for n in names:
        stats = {}
        for arm in arms:
            if stop.exists():
                fail(f'stop file {stop}')
            if (arm, n) in reuse:
                run = reuse[(arm, n)]
                log(f'{arm} {n}: reusing {run}')
            else:
                r = runs.run_one(n, bins[arm], a.work / f'run-{arm}-{n}', a.saves, a.db, {}, log, a.release)
                results['runs'].append(dict(r, arm=arm))
                if r['rc'] != 0 or 'passed' not in r['result']:
                    fail(f'{arm} {n}: exit {r["rc"]}, {r["result"] or "no result line"}')
                run = Path(r['run'])
            db = run / 'cache' / 'stats.db'
            if not db.is_file():
                fail(f'{arm} {n}: no {db}')
            stats[arm] = db
        s = runs.SETS[n]
        out = a.work / f'idtrace-{n}'
        cmd = [sys.executable, '-I', 'perf/evidence/0438/idtrace.py', str(s['from']), str(s['turns']), str(out)] + \
              [f'{arm}={p}' for arm, p in stats.items()]
        cp = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True, errors='replace')
        (a.work / f'idtrace-{n}.txt').write_text(cp.stdout + cp.stderr, encoding='utf-8')
        log(f'window {n} {s["from"]}-{s["turns"]}: idtrace rc {cp.returncode}\n' + cp.stdout.strip())
        results['windows'][n] = {'rc': cp.returncode, 'from': s['from'], 'to': s['turns']}
        rc_all |= cp.returncode
    (a.work / 'identity.json').write_text(json.dumps(results, indent=1), encoding='utf-8')
    log('identity: ' + ('IDENTICAL on every window' if rc_all == 0 else 'DIFFERENCES (see idtrace-*.txt)'))
    sys.exit(0 if rc_all == 0 else 1)


if __name__ == '__main__':
    main()
