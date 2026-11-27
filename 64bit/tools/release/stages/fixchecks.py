"""fixchecks (CL step 15): the checks of the fixes the patch carries, on the release's x64 build, and an old save
loading. The scripts are the bugfix team's (research/03-dll-engine-boundary/evidence/4000); each starts the server
through server/run_server.py and runs under quietlock.py run. One at a time, each recorded as it passes:

    0096   0096_run.py --pin-bin <new> --fix-bin <new>: the new build's line is "0096-RESULT PASS" (the empty
           unit-killed notification is fixed); the "pin" arm is the same build (the verdict line compares old against
           new, which is not the question here)
    0084   0084_lock_test.py --arms control,fixed,fixed-nolock with both bins the new build: VERDICT PASS (the
           statistics logger keeps every row while another connection holds a write lock)
    0103   0103_run.py --game tiny --turns 100, arms new-off (AI logging off) and new-on (on): IDENTICAL (AI logging
           does not change the game)
    oldsave server/run_server.py --load <the huge reference save, a copy> --turns <its turn + 2>: exit 0 (a save
           written by an earlier build loads and plays two turns)
"""
from __future__ import annotations

from take_release import StageResult
from stages import _common
from stages.savetest import huge_copy

DEPENDS = ['build']


def checks(ctx, b, base, save, turn):
    new = b['x64_bin']
    return [
        ('0096', ctx.tool('fix0096') + ['--pin-bin', new, '--fix-bin', new, '--out', base / '0096'],
         lambda r: any(ln.startswith('fix: rc 0:') and '0096-RESULT PASS' in ln for ln in r.text.splitlines())),
        ('0084', ctx.tool('fix0084') + ['--pin-bin', new, '--fix-bin', new, '--out', base / '0084', '--turns', '60',
                                        '--arms', 'control,fixed,fixed-nolock'],
         lambda r: r.rc == 0 and 'VERDICT PASS' in r.text),
        ('0103', ctx.tool('fix0103') + ['--game', 'tiny', '--turns', '100', '--out', base / '0103',
                                        '--arm', f'new-off|{new}|0', '--arm', f'new-on|{new}|15', '--ref', 'new-off'],
         lambda r: r.rc == 0 and any(ln.startswith('CMP new-off vs new-on: IDENTICAL') for ln in r.text.splitlines())),
        ('oldsave', ctx.tool('run_server') + ['--load', save, '--turns', turn + 2, '--run', base / 'oldsave',
                                              '--bin', new, '--dll-log-flags', '0', '--db', ctx.desc.test_db,
                                              '--loc-db', ctx.desc.loc_db],
         lambda r: r.rc == 0),
    ]


def run(ctx):
    b = ctx.outputs('build')
    save, stop = huge_copy(ctx)
    if stop:
        return stop
    turn = ctx.desc.reference_saves['huge'].turn
    base = ctx.scratch('fixchecks')
    prev = ctx.previous_outputs().get('checks') or {}
    done = {}
    for name, cmd, passed in checks(ctx, b, base, save, turn):
        if prev.get(name, {}).get('pass'):
            done[name] = prev[name]
            continue
        _common.fresh_dir(base / name)
        # the fix scripts write their servers' output to files of their own, so a refusal of the inner gate would not
        # show in this log: they start under quietlock.py run; run_server.py (oldsave) is a gated launch point
        r = ctx.run(cmd, name=name, gated=(name == 'oldsave'),
                    what=f'{ctx.who} take_release {ctx.tag} fix check {name}')
        ok = bool(passed(r))
        done[name] = {'rc': r.rc, 'pass': ok, 'seconds': round(r.seconds), 'log': str(r.log),
                      'last': r.tail(3).splitlines()}
        ctx.partial(checks=done)
        if not ok:
            return StageResult.failed(f'fix check {name} did not pass (exit {r.rc}); {r.log}\n{r.tail(10)}',
                                      checks=done)
    return StageResult.ok(checks=done)
