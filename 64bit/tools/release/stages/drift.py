"""drift (CL step 14, the Tiny pair only): the same Tiny fixed-seed game to turn 100 with the release's 32-bit DLL
in the headless host and its x64 DLL in the server (tools/drift/run_pair.py, under quietlock.py run: its x86 arm is
not a gated launch point), then server/tools/compare_stats.py over both stats.db. Pass = both arms exit 0, the compare
exits 0 and no table differs. A difference is a stop: x64 != x86 is a bug before the release can be taken. The whole
games of CL step 14 on the testbeds are the validated tier (wave 2).
"""
from __future__ import annotations

import re

from take_release import StageResult
from stages import _common

DEPENDS = ['build']
TURNS = 100


def run(ctx):
    b = ctx.outputs('build')
    out = _common.fresh_dir(ctx.scratch() / 'drift')
    r = ctx.run(ctx.tool('run_pair') + ['--spec', 'tiny', '--turns', TURNS, '--out', out, '--tag', 'tiny',
                                        '--x86-dll', b['i0_dll'], '--srv-bin', b['x64_bin']], name='run_pair',
                what=f'{ctx.who} take_release {ctx.tag} drift pair tiny 0-{TURNS}')
    first = r.lines(r'^first differing turn:')
    cmp = r.lines(r'^compare: exit')
    rows = None
    cmp_file = out / 'tiny-compare.txt'
    if cmp_file.is_file():
        # server/tools/compare_stats.py: "total rows: A <n>, B <n>" then "RESULT: IDENTICAL|DIFFERENT"
        m = re.findall(r'total rows: A (\d+), B (\d+)', cmp_file.read_text(encoding='utf-8', errors='replace'))
        rows = {'x86': int(m[-1][0]), 'x64': int(m[-1][1])} if m else None
    outputs = dict(out=str(out), compare=str(cmp_file), rows=rows, first_diff=first[-1] if first else None)
    if not first or not cmp:
        return StageResult.failed(f'run_pair.py exit {r.rc} without a comparison; {r.log}\n{r.tail(8)}', **outputs)
    if first[-1].strip() != 'first differing turn: None':
        return StageResult.stop(f'x64 != x86 on Tiny 0-{TURNS}: {first[-1].strip()} ({cmp_file})',
                                'find the cause (tools/drift/, research/03-dll-engine-boundary/stl-drift-sites.md) and file a bug; the '
                                'release is not taken while x64 and x86 differ', **outputs)
    if r.rc != 0 or not cmp[-1].startswith('compare: exit 0'):
        return StageResult.failed(f'run_pair.py exit {r.rc}, {cmp[-1]}; {r.log}\n{r.tail(8)}', **outputs)
    return StageResult.ok(**outputs)
