"""branches (CL step 3): the release's fork branches from the carry series, through 3505's tools/release/branches.py
(README section 3). Called without --push (pushing is 3505's and the MC's step). The branch name is content-addressed:
<version>-c<8 hex of the series file and the tag commit>, so a changed series gives new branches and a rerun with the
same inputs replaces its own (--replace).

The series is tools/release/series/carry.tsv, or take_release.py --series FILE (issue 3509: a release copy, recorded in
the state's config as `series`); a stop of branches.py that says "rerun with --series <copy>" is passed on naming the
driver's option. The base is the survey's tag_commit: the tag, or take_release.py --base-commit.

--carry pins: the stage is skipped and the commits of deps/i0.lock and deps/dll.lock are built (the pins' own
release, or a comparison).
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from take_release import REPO, StageResult, sha256_file
from stages import _common

DEPENDS = ['survey']
SERIES = REPO / 'tools/release/series/carry.tsv'


def series_path(ctx) -> Path:
    return Path(ctx.config.get('series') or SERIES)


def inputs(ctx):
    s = series_path(ctx)
    return {'carry': ctx.config.get('carry', 'series'), 'series_sha256': sha256_file(s) if s.is_file() else None,
            'pins': _common.pins(ctx) if ctx.config.get('carry') == 'pins' else None}


def suffix(ctx, tag_commit) -> str:
    # the tag's version (Release-5.4.8 -> 5.4.8); a release name of --base-commit as it is (issue 3509)
    version = ctx.tag[len('Release-'):] if ctx.tag.startswith('Release-') else ctx.tag
    h = hashlib.sha256(Path(series_path(ctx)).read_bytes() + tag_commit.encode()).hexdigest()[:8]
    return f'{version}-c{h}'


SERIES_RERUN_RE = re.compile(r'(?:re)?run with --series <copy>')


def driver_unblock(ctx, text: str) -> str:
    """branches.py's unblock names its own option ("rerun with --series <copy>"); through the driver the option is
    take_release.py's --series (issue 3509), recorded in the state, so a plain rerun afterwards keeps the copy."""
    new = (f'rerun take_release.py {ctx.tag} --series <copy> (recorded in the state: later runs keep it; the copy\'s '
           f'commits must be in the fork clone {ctx.fork})')
    return SERIES_RERUN_RE.sub(lambda _m: new, text)   # a function: the fork path's backslashes stay literal


def run(ctx):
    if ctx.config.get('carry') == 'pins':
        p = _common.pins(ctx)
        return StageResult.skipped('--carry pins: the commits of deps/i0.lock and deps/dll.lock',
                                   i0_commit=p['i0_commit'], i0_branch=p['i0_branch'], x64_commit=p['x64_commit'],
                                   x64_branch=p['x64_branch'], source='deps/*.lock')
    if not series_path(ctx).is_file():
        return StageResult.stop(f'series file {series_path(ctx)} missing',
                                f'land issue 3505 (tools/release/series/carry.tsv), merge it, and run again, or name '
                                f'a series file: take_release.py {ctx.tag} --series <file>')
    tag_commit = ctx.outputs('survey')['tag_commit']
    name = suffix(ctx, tag_commit)
    out = ctx.dir('branches')
    p = _common.pins(ctx)
    res, r = _common.call_entry(ctx, 'branches', '3505',
                                ['--fork', ctx.fork, '--base', tag_commit, '--series', series_path(ctx), '--name', name,
                                 '--out', out, '--replace', '--no-fetch', '--compare-i0', p['i0_commit'],
                                 '--compare-x64', p['x64_commit']], name='branches', heavy=False)
    if res:
        if res.status == 'stop':
            res.unblock = driver_unblock(ctx, res.unblock)
        return res
    heads = {}
    for b in ('i0', 'x64'):
        heads[b] = ctx.git('rev-parse', '--verify', f'refs/heads/{b}-release-{name}', check=False)
        if not heads[b]:
            return StageResult.failed(f'branches.py exit 0 but {b}-release-{name} is not in {ctx.fork}')
    summary = {}
    bj = out / 'branches.json'
    if bj.is_file():
        try:
            data = json.loads(bj.read_text(encoding='utf-8'))
            rows = data.get('rows') or []
            for row in rows:
                summary[row.get('status', '?')] = summary.get(row.get('status', '?'), 0) + 1
            summary['compare'] = data.get('compare')
        except ValueError:
            pass
    return StageResult.ok(i0_commit=heads['i0'], i0_branch=f'i0-release-{name}', x64_commit=heads['x64'],
                          x64_branch=f'x64-release-{name}', source='tools/release/branches.py',
                          branches_json=str(bj), rows=summary)
