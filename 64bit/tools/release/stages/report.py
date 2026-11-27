"""report (validated tier, issue 3506): one report of the release's validation, <work>/validated-report.json and
<work>/validated-report.md, naming every run with its inputs and result, and the verdict:

  validated                          every stage of the tier ok, the whole games x64 = x86
  candidate, parity failed           a whole-game comparison differs (decision 1 of the draft, the owner's: never
                                     "validated" while a comparison fails)
  candidate, validated tier not reached (<stage> <status>: <what>)   anything else

Every validated stage calls finish(ctx, result) as it ends, so the report is current after a stop too; this stage
writes the final one and is ok only with the verdict "validated" (else it stops with the verdict).
"""
from __future__ import annotations

import json
import time

from take_release import CANDIDATE, StageResult
from stages import validated as V

DEPENDS = ['refsave', 'instance', 'wholegames']
JSON_NAME = 'validated-report.json'
MD_NAME = 'validated-report.md'


def _entry_view(e, pending=None):
    """A stage's state entry as the report shows it; `pending` = the StageResult of the stage now ending."""
    if pending is not None:
        return {'status': pending.status, 'what': pending.what, 'unblock': pending.unblock,
                'outputs': dict((e or {}).get('outputs') or {}, **(pending.outputs or {})),
                'started': (e or {}).get('started'), 'ended': time.strftime('%Y-%m-%d %H:%M:%S')}
    if not e:
        return {'status': 'not run', 'outputs': {}}
    stop = e.get('stop') or {}
    return {'status': e.get('status'), 'what': stop.get('what') or e.get('error') or '',
            'unblock': stop.get('unblock') or '', 'outputs': e.get('outputs') or {}, 'started': e.get('started'),
            'ended': e.get('ended')}


def verdict(stages: dict) -> str:
    wg = stages.get('wholegames') or {}
    if (wg.get('outputs') or {}).get('parity') == 'failed':
        return 'candidate, parity failed'
    for name, _cl, _o in CANDIDATE + V.STAGES:
        if name == 'report':
            continue
        s = stages.get(name) or {}
        if s.get('status') not in ('ok', 'skipped'):
            what = f": {s.get('what')}" if s.get('what') else ''
            return f"candidate, validated tier not reached ({name} {s.get('status')}{what})"
    if (wg.get('outputs') or {}).get('parity') != 'passed':
        return 'candidate, validated tier not reached (wholegames: no parity result)'
    return 'validated'


def build(ctx, current=None, result=None) -> dict:
    """The report's data. current/result: the stage now ending and its StageResult (not yet in the state)."""
    st = {}
    for name, cl, owner in CANDIDATE + V.STAGES:
        e = ctx.state.get(name)
        st[name] = dict(_entry_view(e, result if name == current else None), cl_step=cl, owner=owner)
    d = ctx.desc
    rep = {
        'format': 1, 'tag': ctx.tag, 'written': time.strftime('%Y-%m-%d %H:%M:%S'), 'work': str(ctx.work),
        'descriptor': {'upstream_commit': getattr(d, 'upstream_commit', None),
                       'mods': [f'{m.folder} (v {m.version})' for m in (getattr(d, 'mods', None) or [])],
                       'test_db_sha256': getattr(d, 'test_db_sha256', None),
                       'player_dll_sha256': getattr(d, 'player_dll_sha256', None)},
        'settings': V.env_settings(),
        'stages': st,
    }
    b = (st.get('build') or {}).get('outputs') or {}
    rep['builds'] = {k: b.get(k) for k in ('x64_commit', 'i0_commit', 'x64_config', 'x64_bin', 'proxy_dll', 'i0_dll',
                                           'i0ia_dll', 'tools_commit') if b.get(k) is not None}
    rep['verdict'] = verdict(st)
    return rep


def _md(rep) -> str:
    L = [f"# Validation report of {rep['tag']}", '',
         f"**Verdict: {rep['verdict']}**", '',
         f"Written {rep['written']} by tools/release/take_release.py --tier validated (issue 3506); work folder "
         f"`{rep['work']}`; state `state.json` there.", '']
    if rep.get('builds'):
        L += ['## Builds', ''] + [f"- {k}: `{v}`" for k, v in rep['builds'].items()] + ['']
    L += ['## Stages', '', '| Stage | CL | Status | What |', '|---|---|---|---|']
    for name, s in rep['stages'].items():
        what = (s.get('what') or '').replace('|', '/').replace('\n', ' ')[:300]
        L.append(f"| {name} | {s.get('cl_step')} | {s.get('status')} | {what} |")
    L.append('')
    for name in [n for n, _c, _o in V.STAGES if n != 'report']:
        o = (rep['stages'].get(name) or {}).get('outputs') or {}
        if not o:
            continue
        L += [f'## {name}', '']
        for k, v in o.items():
            if isinstance(v, (dict, list)):
                L.append(f'- {k}:')
                L.append('```')
                L.append(json.dumps(v, indent=1, default=str))
                L.append('```')
            else:
                L.append(f'- {k}: {v}')
        L.append('')
    L += ['## Settings', '', '```', json.dumps(rep['settings'], indent=1), '```', '']
    return '\n'.join(L)


def write(ctx, current=None, result=None) -> dict:
    rep = build(ctx, current, result)
    (ctx.work / JSON_NAME).write_text(json.dumps(rep, indent=1, default=str) + '\n', encoding='utf-8')
    (ctx.work / MD_NAME).write_text(_md(rep), encoding='utf-8')
    return rep


def finish(ctx, result: StageResult) -> StageResult:
    """Rewrite the report with the ending stage's result, then return that result unchanged. A report that cannot be
    written does not change the stage's result (it is said in the driver's log)."""
    try:
        rep = write(ctx, ctx.stage, result)
        ctx.say(f'report: {ctx.work / MD_NAME} ({rep["verdict"]})')
    except Exception as e:  # noqa: BLE001 - the report must never hide the stage's own result
        ctx.say(f'report: could not be written: {type(e).__name__}: {e}')
    return result


def run(ctx):
    rep = write(ctx, 'report', StageResult.ok())
    v = rep['verdict']
    out = dict(report=str(ctx.work / MD_NAME), report_json=str(ctx.work / JSON_NAME), verdict=v)
    if v != 'validated':
        res = StageResult.stop(f'verdict: {v}', 'read the report; fix what it names and run the tier again', **out)
        write(ctx, 'report', res)
        return res
    return StageResult.ok(**out)
