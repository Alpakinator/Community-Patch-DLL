"""Ticket 4801 (epic 0504 P1): train a PGO profile of the x64 DLL and write its lock file.

    python -I tools/x64-dll/pgo/train.py --dll <pgo-instr build>/CvGameCore_Expansion2.dll --template-bin <pin layout>/bin
        --work C:/c5-64bit-work/4801/train --raw F:/c5-64bit-work/4801/raw --profile F:/c5-64bit-work/4801/profile
        --saves <folder with 0438's two saves> --db <0438's statistics-on Civ5DebugDatabase.db> [--sets H,M,S]
        [--lock tools/x64-dll/pgo/pgo.lock]
    On the VP 5.5 pins (ticket 0774): --saves <folder with 0771's VP55-HUGE_0350-B and HI-0771-LC21B55-T200>, no --db;
    the sets default to G,L,S and the release to deps/dll.lock's base (runs.DEFAULT_SETS); the lock lines name the
    profile as <pgo_dir>/<name> (setting pgo_dir).

Steps: check the DLL is a pgo-instr build (its marker) and the inputs are 0438's (SHA-256); make a layout = the pin
layout's bin with the instrumented DLL; play each window of runs.SETS headless (server/run_server.py, gated, one at a
time) with LLVM_PROFILE_FILE=<raw>/<set>-%p.profraw (the DLL's flush object writes it at TerminateProcess); require
exit 0, a "passed" result and exactly one non-empty raw profile per window; `llvm-profdata merge` them into
<profile>/vp64-<pin commit 9>.profdata; write the lock file (pin commit, profile SHA-256 and size, training set with
each input's SHA-256, tool versions). Exit 0 only when every step succeeded; a stop file <work>/STOP ends it before
the next step. The profile itself is never committed (MC #4429 (2)).
"""
from __future__ import annotations

import argparse
import importlib.util
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
LLVM_BIN = Path(r'C:\Program Files\LLVM\bin')
PROFDATA = LLVM_BIN / 'llvm-profdata.exe'


def _load_build():
    spec = importlib.util.spec_from_file_location('x64build_4801', HERE.parent / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def version_line(cmd) -> str:
    """The first output line that names a version ("LLVM version 19.1.7", "clang version 19.1.7")."""
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    lines = [l.strip() for l in (cp.stdout + cp.stderr).splitlines() if l.strip()]
    return next((l for l in lines if 'version' in l.lower()), lines[0] if lines else '?')


def tool_versions() -> dict:
    b = _load_build()
    return {'llvm-profdata': version_line([str(PROFDATA), '--version']),
            'clang-cl': version_line([str(b.CLANG_CL), '--version']),
            'profile runtime': f'{b.PGO_RT_LIB} sha256 {runs.sha256(b.PGO_RT_DIR / b.PGO_RT_LIB)}'}


def dll_lock_lines(rec: dict, keep_file: str) -> str:
    """The `profile` and `profile-file` lines of deps/dll.lock for this training (ticket 0513; the form
    tools/x64-dll/build.py lock_profile() reads): the first line structured, the training set and tools after it."""
    ind = ' ' * 13
    lines = [f'profile      = sha256 {rec["profile_sha256"]} bytes {rec["profile_bytes"]} pin {rec["pin"]} name '
             f'{rec["profile_name"]}',
             f'{ind}trained {rec["trained"]} by tools/x64-dll/pgo/train.py on the pgo-instr build {rec["instrumented_dll"]}']
    for r in rec['training']:
        lines.append(f'{ind}training {r["set"]}: {Path(str(r["input"])).name} turns {r["from"]}->{r["turns"]}, raw '
                     f'{r["raw_bytes"]} B sha256 {r["raw_sha256"]}')
    lines.append(f'{ind}tools: ' + '; '.join(f'{k} {v}' for k, v in rec['tools'].items()))
    lines.append(f'{ind}{rec["summary"]}')
    lines.append(f'profile-file = {keep_file}')
    return '\n'.join(lines) + '\n'


def write_lock(path: Path, rec: dict):
    lines = ['# PGO profile of the x64 DLL (tickets 4801, 0513; research/08-performance/speedup-p1-pgo.md).',
             '# Written by tools/x64-dll/pgo/train.py. The profile file is NOT committed (MC #4429 (2)); it is kept by',
             '# its SHA-256. The build input is deps/dll.lock\'s `profile` line (ticket 0513); this file is the training',
             '# record of the last train.py run (<work>/dll-lock-profile.txt has the lock lines).',
             f'pin          = {rec["pin"]}',
             f'profile      = {rec["profile_name"]}',
             f'sha256       = {rec["profile_sha256"]}',
             f'bytes        = {rec["profile_bytes"]}',
             f'trained      = {rec["trained"]}',
             f'instrumented = {rec["instrumented_dll"]}']
    for r in rec['training']:
        lines.append(f'training     = {r["set"]}: {r["input"]} turns {r["from"]}->{r["turns"]}, raw {r["raw_bytes"]} B '
                     f'sha256 {r["raw_sha256"]}, wall {r["wall_s"]} s')
    for k, v in rec['tools'].items():
        lines.append(f'tool         = {k}: {v}')
    lines.append(f'summary      = {rec["summary"]}')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--dll', type=Path, required=True)
    ap.add_argument('--template-bin', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--raw', type=Path, required=True)
    ap.add_argument('--profile', type=Path, required=True, help='output folder of the merged profile')
    ap.add_argument('--saves', type=Path, required=True)
    ap.add_argument('--db', type=Path, default=None, help='0438\'s statistics-on database (sets H and M only)')
    ap.add_argument('--release', default=None,
                    help='the VP release the windows run on (C5_RELEASE of server/run_server.py; default '
                         '$C5_RELEASE, else deps/dll.lock base: ticket 0774)')
    ap.add_argument('--sets', default=None, help='default by the release: ' +
                    '; '.join(f'{k} {v}' for k, v in runs.DEFAULT_SETS.items()))
    ap.add_argument('--lock', type=Path, default=HERE / 'pgo.lock')
    ap.add_argument('--keep-file', default=None,
                    help='deps/dll.lock profile-file as written in the lock (ticket 0774: no machine path, the '
                         'default <pgo_dir>/<name>, setting pgo_dir of tools/release/settings.py)')
    a = ap.parse_args()
    a.release = a.release or os.environ.get('C5_RELEASE', '').strip() or runs.lock_release()
    a.sets = a.sets or runs.DEFAULT_SETS.get(a.release, 'S')
    b = _load_build()
    a.work.mkdir(parents=True, exist_ok=True)
    logf = open(a.work / 'train.log', 'a', encoding='utf-8')

    def log(t):
        print(t, flush=True)
        logf.write(t + '\n')
        logf.flush()

    def fail(msg):
        log(f'train: FAILED: {msg}')
        sys.exit(1)

    names = [n.strip() for n in a.sets.split(',') if n.strip()]
    if not names or any(n not in runs.SETS for n in names):
        fail(f'--sets {a.sets}: each of {", ".join(runs.SETS)}')
    if b.dll_config(a.dll) != b.PGO_INSTR:
        fail(f'{a.dll}: not a {b.PGO_INSTR} build (marker {b.dll_config(a.dll)!r})')
    bad = runs.check_release(a.release, names) + runs.check_inputs(a.saves, a.db, names)
    if bad:
        fail('; '.join(bad))
    pin = b.read_dll_lock()['commit']
    stop = a.work / 'STOP'
    bin_dir = runs.make_layout(a.template_bin, a.dll, a.work / 'x64-pgo-instr-layout')
    log(f'{time.strftime("%Y-%m-%d %H:%M:%S")} train: layout {bin_dir}, DLL sha256 {runs.sha256(a.dll)}')
    a.raw.mkdir(parents=True, exist_ok=True)
    training = []
    for n in names:
        if stop.exists():
            fail(f'stop file {stop}')
        for old in a.raw.glob(f'{n}-*.profraw'):
            old.unlink()
        r = runs.run_one(n, bin_dir, a.work / f'run-{n}', a.saves, a.db,
                         {'LLVM_PROFILE_FILE': str(a.raw / f'{n}-%p.profraw')}, log, a.release)
        raws = sorted(a.raw.glob(f'{n}-*.profraw'))
        if r['rc'] != 0 or 'passed' not in r['result']:
            fail(f'{n}: run exit {r["rc"]}, {r["result"] or "no result line"} ({r["run"]})')
        if len(raws) != 1 or raws[0].stat().st_size == 0:
            fail(f'{n}: want one non-empty raw profile, found {[(p.name, p.stat().st_size) for p in raws]}')
        s = runs.SETS[n]
        training.append({'set': n, 'input': s.get('load') or s['spec'], 'from': s['from'], 'turns': s['turns'],
                         'raw': str(raws[0]), 'raw_bytes': raws[0].stat().st_size,
                         'raw_sha256': runs.sha256(raws[0]), 'wall_s': r['wall_s'], 'run': r['run']})
    if stop.exists():
        fail(f'stop file {stop}')
    a.profile.mkdir(parents=True, exist_ok=True)
    out = a.profile / f'vp64-{pin[:9]}.profdata'
    cp = subprocess.run([str(PROFDATA), 'merge', '-o', str(out)] + [t['raw'] for t in training],
                        capture_output=True, text=True)
    log(f'llvm-profdata merge rc {cp.returncode} {cp.stdout.strip()} {cp.stderr.strip()}')
    if cp.returncode != 0 or not out.is_file():
        fail('llvm-profdata merge')
    summ = subprocess.run([str(PROFDATA), 'show', '--detailed-summary', str(out)], capture_output=True, text=True)
    (a.work / 'profile-summary.txt').write_text(summ.stdout + summ.stderr, encoding='utf-8')
    total = [l for l in summ.stdout.splitlines() if l.startswith(('Total functions', 'Maximum function count'))]
    rec = {'pin': pin, 'profile_name': out.name, 'profile_sha256': runs.sha256(out), 'profile_bytes': out.stat().st_size,
           'trained': time.strftime('%Y-%m-%d %H:%M %z'), 'instrumented_dll': f'sha256 {runs.sha256(a.dll)}',
           'training': training, 'tools': tool_versions(), 'summary': '; '.join(total), 'release': a.release}
    (a.work / 'train.json').write_text(json.dumps(rec, indent=1), encoding='utf-8')
    write_lock(a.lock, rec)
    (a.work / 'dll-lock-profile.txt').write_text(dll_lock_lines(rec, a.keep_file or f'<pgo_dir>/{out.name}'), encoding='utf-8')
    log(f'OK: {out} sha256 {rec["profile_sha256"]} {rec["profile_bytes"]} B; lock {a.lock}; deps/dll.lock lines in '
        f'{a.work / "dll-lock-profile.txt"}')


if __name__ == '__main__':
    main()
