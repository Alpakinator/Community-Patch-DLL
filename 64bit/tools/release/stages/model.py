"""model (CL step 10): regenerate the proxy model from the release's x64 source and compare it with proxy/model/
(3503's proxy/gen/regen_model.py; README section 2a). Exit 0 = equal: ok; 1 = different: a stop (a person reviews the
diff, regenerates and commits the model); 2 = refused: failed.
"""
from __future__ import annotations

from take_release import StageResult
from stages import _common

DEPENDS = ['lock', 'build']


def run(ctx):
    b, lk = ctx.outputs('build'), ctx.outputs('lock')
    out = _common.fresh_dir(ctx.work / 'model')
    args = ['--src', b['x64_src'], '--out', out, '--stop-file', ctx.work / 'STOP']
    args += ['--lock', lk['dll_lock']] if lk.get('dll_lock') else ['--unlocked', '--commit', b['x64_commit'][:9]]
    missing = ctx.tool_missing('model')
    if missing:
        return _common.not_built_yet('3503', f'{missing} missing')
    r = ctx.run(ctx.tool('model') + [str(a) for a in args], name='model',
                what=f'{ctx.who} take_release {ctx.tag} proxy model')
    if r.rc == 0:
        return StageResult.ok(out=str(out), equal=True)
    if r.rc == 1:
        return StageResult.stop(f'the model differs from proxy/model (diff in {out})',
                                'review the diff, regenerate and commit the model (CL 10; proxy-design.md 1), then '
                                'run again', out=str(out))
    return StageResult.failed(f'regen_model.py exit {r.rc} (refused); {r.log}\n{r.tail(8)}')
