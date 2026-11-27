"""savetest (CL step 13): the three-part save test (D10/D24) of the release's builds, tools/d10/run_d10.py (a gated
launch point; 20-25 min with --jobs 6): the 32-bit plain DLL of the layout against the x64 layout's bin/, the huge
reference save of the descriptor (a copy in <work>/inputs/, checked by SHA-256), --order-exception none,
--d18-x86 yes --d18-x64 yes, AI logging off. Pass = run_d10 exit 0 and report.json "pass"; then the part-2 row counts
(x86 direct = x64 direct) are compared with the descriptor's reference_counts: a difference is a stop (a release
changes the game, so a new release records its own counts after checking x86 = x64). The descriptor is the candidate
testdata wrote (issue 3512: ctx.desc after testdata; a new release's has no reference_counts, so its rows are recorded
only), and run_d10.py reads the same file through C5_RELEASE.
"""
from __future__ import annotations

import json
import shutil
from pathlib import Path

from take_release import StageResult, sha256_file

DEPENDS = ['build']
USES = ['testdata']
GAMES = ('tiny', 'standard', 'huge')
PEAK_GB = 3.0        # run_d10's database copies at their peak (2,505 MB deleted at the end of 9045's run)
KEEP_FREE_GB = 25.0  # COMMON 16, D111


def inputs(ctx):
    s = (getattr(ctx.desc, 'reference_saves', None) or {}).get('huge')
    return {'huge': {'path': str(s.path), 'sha256': s.sha256, 'turn': s.turn} if s else None,
            'counts': dict(getattr(ctx.desc, 'reference_counts', None) or {}), 'jobs': ctx.config.get('jobs')}


def huge_copy(ctx):
    """The descriptor's huge reference save copied into <work>/inputs/ under its own name (never the original)."""
    s = (getattr(ctx.desc, 'reference_saves', None) or {}).get('huge')
    if not s:
        return None, StageResult.stop('the descriptor names no huge reference save',
                                      'add reference_saves.huge to the descriptor (issue 3501), then run again')
    dst = ctx.dir('inputs') / f'{ctx.tag}-HUGE_{s.turn:04d}.Civ5Save'
    if not (dst.is_file() and sha256_file(dst) == s.sha256):
        if not Path(s.path).is_file():
            return None, StageResult.stop(f'the huge reference save {s.path} is missing',
                                          'restore it or name another copy in the descriptor (issue 3501)')
        shutil.copy2(s.path, dst)
    got = sha256_file(dst)
    if got != s.sha256:
        return None, StageResult.stop(f'the huge reference save {s.path} is {got[:16]}..., the descriptor says '
                                      f'{s.sha256[:16]}...', 'fix the descriptor or the save copy (issue 3501)')
    return dst, None


def part2_rows(report: dict) -> dict:
    rows = {}
    for g, data in (report.get('games') or {}).items():
        for c in ((data.get('parts') or {}).get('2') or {}).get('comparisons') or []:
            if c.get('label') == 'x86 direct vs x64 direct' and c.get('ok'):
                rows[g] = c.get('rows')
    return rows


def run(ctx):
    b = ctx.outputs('build')
    save, stop = huge_copy(ctx)
    if stop:
        return stop
    root = ctx.scratch()
    free_gb = shutil.disk_usage(root).free / 2 ** 30
    keep = float(ctx.config.get('keep_free_gb', KEEP_FREE_GB))
    if free_gb < PEAK_GB + keep:
        # COMMON 16 (D111): keep 25 GB free on C: after a start that writes more than 2 GB (run_d10 about 2.5 GB)
        return StageResult.stop(f'{root} has {free_gb:.1f} GB free; the save test writes about {PEAK_GB:g} GB and '
                                f'{keep:g} GB must stay free', 'free space there, or run again with --scratch on '
                                'another drive (say so in the issue: D111), then run again')
    n = 1
    while (root / f'savetest-{n}').exists():
        n += 1
    out = root / f'savetest-{n}'  # run_d10 needs a new folder; earlier attempts are kept
    r = ctx.run(ctx.tool('run_d10') + ['--out', out, '--x86-dll', b['i0_dll'], '--srv-bin', b['x64_bin'],
                                       '--huge-save', save, '--huge-sha256', sha256_file(save),
                                       '--order-exception', 'none', '--d18-x86', 'yes', '--d18-x64', 'yes',
                                       '--jobs', ctx.config.get('jobs', 6)], name='run_d10', gated=True)
    rp = out / 'report.json'
    report = json.loads(rp.read_text(encoding='utf-8')) if rp.is_file() else {}
    if r.rc != 0 or not report.get('pass'):
        return StageResult.failed(f'run_d10.py exit {r.rc}, pass {report.get("pass")}; failures '
                                  f'{(report.get("failures") or [])[:3]}; report {out / "report.txt"}; {r.log}\n'
                                  f'{r.tail(8)}', out=str(out))
    rows = part2_rows(report)
    ref = dict(getattr(ctx.desc, 'reference_counts', None) or {})
    outputs = dict(out=str(out), report=str(out / 'report.txt'), rows=rows, wall_seconds=report.get('wallSeconds'),
                   reference_counts=ref)
    if not ref:
        return StageResult.ok(**outputs, note='the descriptor has no reference_counts: rows recorded only')
    diff = {g: (rows.get(g), ref.get(g)) for g in set(ref) | set(rows) if rows.get(g) != ref.get(g)}
    if diff:
        return StageResult.stop(
            'the save test passed but its x86 = x64 row counts differ from the descriptor\'s reference_counts: '
            + ', '.join(f'{g} {a} (reference {e})' for g, (a, e) in sorted(diff.items())),
            'if the release changes the game (a new release does), record the new counts in the descriptor '
            '(issue 3501) after reading the report; else find what changed the game, then run again', **outputs)
    return StageResult.ok(**outputs)
