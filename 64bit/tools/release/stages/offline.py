"""offline (CL step 12): the offline proxy checks on the x64 build and layout (9045's s13_offline.py), each under
quietlock.py run, one at a time, each recorded as it passes:

    rt        proxy/tools/test_proxy_rt.py --build <x64 out> --scratch <work>/offline/rt
    hutest    proxy/tools/hutest/build_hutest.py --b <x64 out> --src <x64 src> --deps <x64 out>/deps --out ...
    scenarios the same with --scenario all
    verify    proxy/tools/run_proxy_game.py on the layout: Tiny 0-50 through the proxy, strict, local_answers=1
              local_verify=1 test_slot_poll=4 lua_mirror=1, --fail-on-unexpected, the descriptor's databases

Pass = every tool exits 0 (each prints its own PASS/FAIL; the last lines are kept in the outputs).
"""
from __future__ import annotations

from take_release import REPO, StageResult
from stages import _common

DEPENDS = ['build']
VERIFY_INI = ['strict=1', 'local_answers=1', 'local_verify=1', 'test_slot_poll=4', 'lua_mirror=1']


def inputs(ctx):
    return {'test_db': str(getattr(ctx.desc, 'test_db', '')), 'loc_db': str(getattr(ctx.desc, 'loc_db', ''))}


def checks(ctx, b, base):
    out = b['x64_out']
    verify = ctx.tool('run_proxy_game') + [
        '--bin', b['x64_bin'], '--proxy', b['proxy_dll'], '--spec', REPO / 'server/specs/continents-tiny-fixed-seed.json',
        '--turns', '50', '--run', base / 'verify', '--mode', 'proxy', '--fail-on-unexpected']
    for kv in VERIFY_INI:
        verify += ['--ini', kv]
    if getattr(ctx.desc, 'test_db', None):
        verify += ['--db', ctx.desc.test_db]
    if getattr(ctx.desc, 'loc_db', None):
        verify += ['--loc-db', ctx.desc.loc_db]
    return [
        ('rt', ctx.tool('test_proxy_rt') + ['--build', out, '--scratch', base / 'rt']),
        ('hutest', ctx.tool('hutest') + ['--b', out, '--src', b['x64_src'], '--deps', f'{out}/deps',
                                         '--out', base / 'hutest']),
        ('scenarios', ctx.tool('hutest') + ['--b', out, '--src', b['x64_src'], '--deps', f'{out}/deps',
                                            '--out', base / 'hutest-scenarios', '--scenario', 'all']),
        ('verify', verify),
    ]


def run(ctx):
    b = ctx.outputs('build')
    prev = ctx.previous_outputs().get('checks') or {}
    base = ctx.scratch('offline')
    done = {}
    for name, cmd in checks(ctx, b, base):
        if prev.get(name, {}).get('rc') == 0:
            done[name] = prev[name]
            continue
        _common.fresh_dir(base / {'rt': 'rt', 'verify': 'verify', 'hutest': 'hutest',
                                  'scenarios': 'hutest-scenarios'}[name])
        r = ctx.run(cmd, name=name, what=f'{ctx.who} take_release {ctx.tag} offline {name}')
        done[name] = {'rc': r.rc, 'seconds': round(r.seconds), 'log': str(r.log),
                      'last': r.tail(3).splitlines()}
        ctx.partial(checks=done)
        if r.rc != 0:
            return StageResult.failed(f'offline check {name} exit {r.rc}; {r.log}\n{r.tail(10)}', checks=done)
    return StageResult.ok(checks=done)
