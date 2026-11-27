"""purity (CL step 11): the purity file for the release's x64 DLL (3503's proxy/tools/derive_purity.py; README
sections 2a and 4): <work>/purity/purity.tsv bound to the DLL's SHA-256, and report.tsv with each dropped entry.
Then (issue 0480) the full proxy/tools/test_purity.py (unit, ASan, derive with its seeded cases, refusals, check,
regen) on a source where every audited row is kept: the release's x64 source when derive dropped none, else a plain
copy of the baseline's `audited_at` commit from the fork (proxy/model/purity-baseline.json). Until 0480 nothing ran the
full test but a hand: its derive checks failed on main for ten hours unseen (0478). About 110 s [L 2026-11-17].
Exit 0 = written and the test passed (entries may be dropped: counted in the outputs); else failed.
"""
from __future__ import annotations

import json
import re

from take_release import REPO, StageResult
from stages import _common

DEPENDS = ['build', 'model']
TEST_SUMMARY_RE = re.compile(r'^test_purity: (\d+)/(\d+) checks passed')


def _rows(p):
    if not p.is_file():
        return None
    return [ln for ln in p.read_text(encoding='utf-8', errors='replace').splitlines()
            if ln.strip() and not ln.startswith('#')]


def _audited_src(ctx):
    """(StageResult or None, source folder): a plain copy of the baseline's audited_at commit (the source the frozen
    audit was located in, so every row is kept there). Config `purity_audited_src` is a test hook."""
    hook = ctx.config.get('purity_audited_src')
    if hook:
        return None, hook
    base = REPO / 'proxy' / 'model' / 'purity-baseline.json'
    try:
        commit = json.loads(base.read_text(encoding='utf-8'))['audited_at']
    except (OSError, ValueError, KeyError) as e:
        return StageResult.failed(f'{base}: no audited_at ({e})'), None
    from stages import build   # the git archive helper (reused when its marker names the same commit)
    try:
        return None, str(build._archive(ctx, commit, ctx.scratch('src') / f'purity-audited-{commit[:8]}'))
    except (RuntimeError, OSError) as e:
        return StageResult.failed(f'the full purity test needs a source where every row is kept; derive dropped rows '
                                  f'on the release\'s source and the audited commit {commit[:9]} could not be copied '
                                  f'from the fork: {e}'), None


def full_test(ctx, b, dll, dropped):
    """Issue 0480: proxy/tools/test_purity.py in full. (StageResult or None, outputs)."""
    if dropped:
        res, src = _audited_src(ctx)
        if res:
            return res, {}
        why = f'the audited source (derive dropped {dropped} row(s) on the release\'s)'
    else:
        src, why = b['x64_src'], 'the release\'s x64 source'
    scratch = ctx.scratch('purity-test')   # reused: the test replaces its own subfolders (about 170 MB)
    ctx.say('full purity test on', why, src)
    res, r = _common.call_entry(ctx, 'purity_test', '0480', ['--src', src, '--dll', dll, '--scratch', scratch],
                                name='test', heavy=True)
    if res:
        if res.status == 'stop' and 'not built yet' not in res.what:   # the test has no stop exit: 10 is a failure
            return StageResult.failed(f'test_purity.py exit 10; {r.log}'), {}
        if res.status == 'failed':
            fails = r.lines(r'^FAIL ') if r else []
            return StageResult.failed(f'the full purity test failed (proxy/tools/test_purity.py on {why}): '
                                      f'{len(fails)} check(s) failed; see {r.log if r else "-"}\n'
                                      + '\n'.join(fails[:8])), {}
        return res, {}
    m = next((TEST_SUMMARY_RE.match(x) for x in reversed(r.text.splitlines()) if TEST_SUMMARY_RE.match(x)), None)
    if not m or m.group(1) != m.group(2):
        return StageResult.failed(f'test_purity.py exit 0 without a passing summary line; see {r.log}'), {}
    return None, {'test_checks': f'{m.group(1)}/{m.group(2)}', 'test_src': 'audited' if dropped else 'release'}


def run(ctx):
    b = ctx.outputs('build')
    out = _common.fresh_dir(ctx.work / 'purity')
    dll = f'{b["x64_bin"]}/CvGameCore_Expansion2.dll'
    res, r = _common.call_entry(ctx, 'purity', '3503', ['--src', b['x64_src'], '--dll', dll, '--out', out,
                                                          '--commit', b['x64_commit']],
                                name='purity', heavy=True)
    if res:
        if res.status == 'stop' and 'not built yet' not in res.what:  # the tool has no stop exit: 10 is a failure
            return StageResult.failed(f'derive_purity.py exit 10; {r.log}')
        return res
    tsv = out / 'purity.tsv'
    rows = _rows(tsv)
    if rows is None:
        return StageResult.failed(f'derive_purity.py exit 0 but {tsv} is missing')
    rep = _rows(out / 'report.tsv') or []  # kind, key, decision (kept / dropped ...), reason; first row the header
    dropped = sum(1 for x in rep[1:] if len(x.split('\t')) > 2 and x.split('\t')[2] != 'kept')
    res, test = full_test(ctx, b, dll, dropped)
    if res:
        return res
    return StageResult.ok(purity_tsv=str(tsv), report=str(out / 'report.tsv'),
                          methods=sum(1 for x in rows if x.startswith('method\t')),
                          lua=sum(1 for x in rows if x.startswith('lua\t')), dropped=dropped, **test)
