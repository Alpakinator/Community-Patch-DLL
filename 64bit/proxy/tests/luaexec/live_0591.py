"""Ticket 0591: live round trip of the harness Lua channel through the proxy whose runner compiles without loadstring,
on a Goldberg instance of the OLD build (4390913). Derived from research/06-tooling-and-workflows/evidence/2600/0498/
run_0498.py (mods form only, set-proxy instead of the installer).

    python -I proxy/tests/luaexec/live_0591.py --name c5h-a --bin <layout>/bin --proxy <proxy dll> --ev <evidence dir>
                                               --runs <dir> --stop-file F [--turns 2] [--from STEP] [--until STEP]

The new build's sandbox is reproduced on the old build: through the channel itself, _G.loadstring is saved into the
channel's persistent table and set to nil (the UI's own users of loadstring ran at load: EUI's tooltip include and
CPK's captured local), every check below runs without it, and it is put back before the stop.

Steps (each checks the stop file first; any failure ends the driver non-zero and leaves the state for a person):
  verify    provision.py verify NAME --quick; the EXE must be build 4390913 (exe_build.py), never 26577625
  setproxy  provision.py set-proxy NAME --bin BIN --proxy PROXY (default ini: mode=on, dev layout, pack=0)
  launch    frontend.prepare(cp, vp, eui, a new duel game), cli.py launch (frame cap, guard, start-up watch, watcher)
  boot      the newest VP64 run's vpproxy.log shows server attached and kRt_Boot ok, no FALLBACK; load screen ready
  dismiss   frontend.py -i NAME dismiss (the channel), exit 0
  ingame    frontend.probe_in_game ok
  channel   the round trips (see CHECKS): loadstring nil'd through the channel, then results, errors, print, states
  turns     N turns by turns.py end + wait (the CLI, through the channel), loadstring still nil; box check each turn
  unnil     loadstring put back from the persistent table; type(loadstring) == 'function'
  autosave  turns.stop_readiness (newest save unchanged for 10 s), posted on thread 0591 before the stop
  stop      cli.py stop (human-turn hook), cli.py leftovers
  original  provision.py set-proxy NAME --original --runs-to RUNS
  restore   frontend.py restore, provision.py verify NAME
A box: the driver stops and says so; nothing is pressed.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO))
OWNER = 'impl-B-0591'
NEW_GAME = 'map=Continents;size=WORLDSIZE_DUEL;ais=1;minors=2'
STEPS = ['verify', 'setproxy', 'launch', 'boot', 'dismiss', 'ingame', 'channel', 'turns', 'unnil', 'autosave', 'stop',
         'original', 'restore']
HOME = os.path.expanduser('~')

# (label, state, code, predicate on the result dict, what it shows)
NIL = "vp.saved_loadstring_0591 = G.loadstring\nG.loadstring = nil\nreturn type(vp.saved_loadstring_0591), G.loadstring == nil"
CHECKS = [
    ('nil-loadstring', None, NIL,
     lambda r, v: r.get('ok') is True and v == ['function', True], 'loadstring saved and set to nil in _G'),
    ('result', None, 'return Game.GetGameTurn(), type(G.loadstring), 6 * 7',
     lambda r, v: r.get('ok') is True and isinstance(v[0], int) and v[1:] == ['nil', 42],
     'a request compiles and answers with _G.loadstring nil'),
    # Info only: the Main environment may hold its own copy of loadstring made at load on the old build.
    ('main-env-view', None, 'return type(loadstring), type(load)',
     lambda r, v: r.get('ok') is True, 'what the Main environment sees (info: a copy made at load may remain)'),
    ('syntax-error', None, 'return +',
     lambda r, v: r.get('ok') is False and 'luaexec:' in (r.get('error') or ''),
     'a syntax error answers ok=0 with the luaexec:N: chunk name'),
    ('runtime-error', None, "error('boom-0591')",
     lambda r, v: r.get('ok') is False and 'boom-0591' in (r.get('error') or ''), 'a run-time error answers ok=0'),
    ('print', None, "print('p-0591', 1) return true",
     lambda r, v: r.get('ok') is True and 'p-0591\t1' in (r.get('printed') or []), 'print is captured'),
    ('persist', None, 'vp.n0591 = (vp.n0591 or 0) + 1 return vp.n0591',
     lambda r, v: r.get('ok') is True and isinstance(v[0], int), 'the persistent table'),
    ('state-InGame', 'InGame', 'return ContextPtr ~= nil, StateName',
     lambda r, v: r.get('ok') is True and v[0] is True, 'a named UI state (InGame)'),
    ('state-G', '_G', 'return type(loadstring), type(load)',
     lambda r, v: r.get('ok') is True and v == ['nil', 'function'], '_G itself: loadstring nil, load a function'),
    ('state-unknown', 'NoSuchState0591', 'return 1',
     lambda r, v: r.get('ok') is False and 'NoSuchState0591' in (r.get('error') or json.dumps(r, default=str)),
     'an unknown state answers ok=0'),
]
UNNIL = "G.loadstring = vp.saved_loadstring_0591\nvp.saved_loadstring_0591 = nil\nreturn type(G.loadstring)"


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


class Fail(Exception):
    pass


def now():
    return time.strftime('%Y-%m-%d %H:%M:%S')


def scrub(s):
    # The doubled-backslash form too: a dict's repr in a log line doubles them (0591, MC-B 00:05).
    for h in {HOME.replace('\\', '\\\\'), HOME, HOME.replace('\\', '/')}:
        s = s.replace(h, '%USERPROFILE%')
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--name', default='c5h-a')
    ap.add_argument('--bin', required=True, help='the x64 layout bin folder (server.exe, the x64 DLL, libraries)')
    ap.add_argument('--proxy', required=True, help='the proxy build under test')
    ap.add_argument('--ev', required=True)
    ap.add_argument('--runs', required=True, help='where set-proxy --original moves the VP64 run folders')
    ap.add_argument('--stop-file', required=True)
    ap.add_argument('--turns', type=int, default=2)
    ap.add_argument('--from', dest='start', default=STEPS[0], choices=STEPS)
    ap.add_argument('--until', dest='until', default=STEPS[-1], choices=STEPS)
    a = ap.parse_args()
    name = a.name
    ev = Path(a.ev)
    ev.mkdir(parents=True, exist_ok=True)
    summary = ev / 'summary.txt'

    def say(msg):
        line = scrub('%s %s' % (now(), msg))
        print(line, flush=True)
        with open(summary, 'a', encoding='utf-8') as f:
            f.write(line + '\n')

    def run(cmd, label, ok=(0,), timeout=1800):
        out = ev / ('%s.log' % label)
        say('RUN %s: %s' % (label, ' '.join(str(c) for c in cmd)))
        r = subprocess.run([str(c) for c in cmd], cwd=str(REPO), capture_output=True, text=True, timeout=timeout,
                           encoding='utf-8', errors='replace')
        out.write_text(scrub('$ %s\n%s\n--- stderr\n%s\nexit %d\n' % (' '.join(str(c) for c in cmd), r.stdout,
                                                                     r.stderr, r.returncode)), encoding='utf-8')
        say('  exit %d (%s)' % (r.returncode, out.name))
        if r.returncode not in ok:
            raise Fail('%s: exit %d; last lines: %s' % (label, r.returncode, (r.stdout + r.stderr).strip()[-600:]))
        return r

    py = [sys.executable, '-I']

    def box_check(where):
        box = subprocess.run(py + ['tools/harness/dialogwatch.py', 'marker', '--instance', name],
                             cwd=str(REPO), capture_output=True, text=True)
        if box.returncode == 8:
            raise Fail('a box is open at %s (dialogwatch marker): %s' % (where, box.stdout.strip()[-400:]))

    os.environ['C5_POOL_OWNER'] = OWNER
    os.environ['C5_JOB_OWNER'] = OWNER
    from tools.harness.instance import Instance
    from tools.harness import frontend, turns, lua
    inst = Instance.load(name)
    mg = Path(inst.user_dir)
    proxy_path = mg / 'MODS' / '(1) Community Patch' / 'CvGameCore_Expansion2.dll'
    start, until = STEPS.index(a.start), STEPS.index(a.until)
    step = a.start
    results = []

    def ask(label, state, code, timeout=20.0):
        box_check(label)
        try:
            r = lua.try_lua(inst, code, state=state, timeout=timeout)
        except lua.LuaTimeout as e:
            raise Fail('%s: channel timeout: %s' % (label, e))
        v = lua.values(r) if r.get('ok') else []
        (ev / ('lua-%s.json' % label)).write_text(scrub(json.dumps({'state': state, 'code': code, 'result': r,
                                                                    'values': v}, indent=1, default=str)),
                                                  encoding='utf-8')
        return r, v

    try:
        for step in STEPS[start:until + 1]:
            if Path(a.stop_file).exists():
                raise Fail('stop file %s' % a.stop_file)
            say('== step %s (%s)' % (step, name))
            if step == 'verify':
                run(py + ['tools/instances/provision.py', 'verify', name, '--quick'], '0-verify-before')
                from tools.harness import exe_build
                try:
                    info = exe_build.check_install(inst.install_dir, allow=False, what='the instance install')
                except exe_build.UnknownExeBuild as e:
                    raise Fail(str(e))
                say('  EXE: %s' % exe_build.describe(info))
                if not info.get('known'):
                    raise Fail('the instance EXE is not build 4390913: %s' % info)
            elif step == 'setproxy':
                run(py + ['tools/instances/provision.py', 'set-proxy', name, '--bin', a.bin, '--proxy', a.proxy],
                    '1-set-proxy')
                h = sha256(proxy_path)
                if h != sha256(a.proxy):
                    raise Fail('installed proxy %s is not the build under test' % h)
                say('  proxy in place: %s' % h)
            elif step == 'launch':
                # MC #6596: the guarded front-end files compared after the restore against their pre-prepare hashes
                fe = frontend._paths(inst)[3]
                pre = {n: sha256(fe / n) for n in frontend.PATCHED + frontend.RECORDED if (fe / n).is_file()}
                (ev / '2-frontend-pre-prepare.json').write_text(json.dumps(pre, indent=1), encoding='utf-8')
                say('  front-end files before prepare: %s' % {k: v[:12] for k, v in pre.items()})
                r = frontend.prepare(inst, ['cp', 'vp', 'eui'], None, new_game=NEW_GAME)
                (ev / '2-frontend-prepare.json').write_text(scrub(json.dumps(r, indent=1, default=str)),
                                                            encoding='utf-8')
                r = run(py + ['tools/harness/cli.py', '-i', name, 'launch'], '2-launch')
                for ln in r.stdout.splitlines():
                    if re.match(r'(launched|frame cap|start-up watch|audio|guard|dialog)', ln):
                        say('    | ' + ln[:300])
                if not re.search(r'^start-up watch \(issue 9010\): ok,', r.stdout, re.M):
                    raise Fail('start-up watch not ok (COMMON 14 c); see 2-launch.log')
                g = re.search(r'guard log (\S.*)$', r.stdout, re.M)
                gl = Path(g.group(1).strip()) if g else None
                if not (gl and gl.is_file()):
                    raise Fail('no guard log found in the launch output (COMMON 14 c)')
                gt = gl.read_text(encoding='utf-8', errors='replace')
                (ev / '2-guard.log').write_text(scrub(gt), encoding='utf-8')
                bad = [x for x in gt.splitlines() if 'not installed' in x.lower() or 'fail' in x.lower()]
                say('  guard log %d lines; "not installed"/"fail": %d' % (len(gt.splitlines()), len(bad)))
                if bad:
                    raise Fail('guard part not installed or failed (COMMON 14 c): %s' % bad[:3])
            elif step == 'boot':
                t0 = time.time()
                last = None
                while True:
                    if Path(a.stop_file).exists():
                        raise Fail('stop file')
                    box_check('boot')
                    if not inst.refresh():
                        raise Fail('the game process is gone (crash?)')
                    logs = sorted((mg / 'Logs' / 'VP64' / 'run').glob('*/vpproxy.log'), key=lambda p: p.stat().st_mtime)
                    text = logs[-1].read_text(encoding='utf-8', errors='replace') if logs else ''
                    s = frontend.probe_in_game(inst)
                    if s.kind != last:
                        last = s.kind
                        say('  probe at %d s: [%s] %s' % (time.time() - t0, s.kind, s.reason[:160]))
                    if 'kRt_Boot' in text and s.kind in ('load_screen_ready', 'in_game', 'paused'):
                        break
                    if time.time() - t0 > 900:
                        raise Fail('no load screen within 900 s; last probe [%s] %s' % (s.kind, s.reason))
                    time.sleep(5)
                (ev / '3-vpproxy.log').write_text(scrub(text), encoding='utf-8')
                checks = {'server attached': 'server attached' in text, 'kRt_Boot ok': bool(re.search(r'kRt_Boot ok', text)),
                          'no FALLBACK': 'FALLBACK' not in text,
                          'Lua channel thread': 'persistent engine Lua thread VP_LUAEXEC' in text}
                for k, v in checks.items():
                    say('  check %-24s %s' % (k, 'ok' if v else 'FAIL'))
                if not all(checks.values()):
                    raise Fail('boot checks failed: %s' % [k for k, v in checks.items() if not v])
            elif step == 'dismiss':
                box_check('dismiss')
                r = run(py + ['tools/harness/frontend.py', '-i', name, 'dismiss'], '3b-frontend-dismiss', ok=(0, 1))
                if r.returncode != 0:
                    raise Fail('frontend.py dismiss exit %d' % r.returncode)
            elif step == 'ingame':
                t_end = time.time() + 120
                while True:
                    box_check('ingame')
                    s = frontend.probe_in_game(inst)
                    if s.ok is True or time.time() > t_end:
                        break
                    time.sleep(3)
                say('  probe: [%s] %s' % (s.kind, s.reason))
                if s.ok is not True:
                    raise Fail('not in game: [%s] %s' % (s.kind, s.reason))
            elif step == 'channel':
                failed = []
                for label, state, code, pred, what in CHECKS:
                    r, v = ask(label, state, code)
                    good = bool(pred(r, v))
                    results.append({'check': label, 'what': what, 'pass': good, 'ok': r.get('ok'), 'values': v,
                                    'env': r.get('env'), 'error': (r.get('error') or '')[:300]})
                    say('  %s %-16s %s (ok=%s env=%s values=%s%s)' % ('PASS' if good else 'FAIL', label, what,
                                                                     r.get('ok'), r.get('env'), v,
                                                                     (' error=%r' % (r.get('error') or '')[:120])
                                                                     if r.get('ok') is False else ''))
                    if not good:
                        failed.append(label)
                    if label == 'nil-loadstring' and not good:
                        raise Fail('could not nil loadstring through the channel')
                (ev / '5-channel-checks.json').write_text(json.dumps(results, indent=1, default=str), encoding='utf-8')
                if failed:
                    raise Fail('channel checks failed: %s' % failed)
            elif step == 'turns':
                for i in range(a.turns):
                    if Path(a.stop_file).exists():
                        raise Fail('stop file')
                    box_check('turn %d' % (i + 1))
                    ts = turns.turn_state(inst)
                    if not ts:
                        raise Fail('turn_state: channel silent')
                    t0 = time.time()
                    run(py + ['tools/harness/turns.py', 'end', '--instance', name], '6-end-turn-%02d' % (i + 1))
                    run(py + ['tools/harness/turns.py', 'wait', '--instance', name, str(ts[0]), '--timeout', '600'],
                        '6-wait-turn-%02d' % (i + 1))
                    ts2 = turns.turn_state(inst)
                    if not ts2 or ts2[0] != ts[0] + 1:
                        raise Fail('turn %s did not advance by one: now %s' % (ts[0], ts2))
                    r, v = ask('turn-%02d-loadstring' % (i + 1), None, 'return type(G.loadstring)')
                    say('  turn %s -> %s in %.1f s; loadstring still %s' % (ts[0], ts2[0], time.time() - t0, v))
                    if v != ['nil']:
                        raise Fail('loadstring is no longer nil after turn %d: %s' % (i + 1, v))
                box_check('after the turns')
            elif step == 'unnil':
                r, v = ask('unnil', None, UNNIL)
                say('  loadstring put back: ok=%s values=%s' % (r.get('ok'), v))
                if not (r.get('ok') is True and v == ['function']):
                    raise Fail('loadstring not restored: %s' % r)
            elif step == 'autosave':
                box_check('autosave')
                rd = turns.stop_readiness(inst, settle_s=10)
                d = {k: getattr(rd, k, None) for k in ('ready', 'reason', 'turn', 'active', 'save')}
                (ev / '7-autosave-check.json').write_text(scrub(json.dumps(d, indent=1, default=str)),
                                                          encoding='utf-8')
                say('  stop readiness: %s' % d)
                if not d.get('ready'):
                    raise Fail('not ready to stop: %s' % d)
                text = ('impl-B-0591 %s: autosave check on %s before the harness stop: turns.stop_readiness ready=%s, '
                        '%s; newest save %s (unchanged for 10 s, turn %s). Stopping with cli.py stop (human-turn hook) '
                        'next.' % (time.strftime('%H:%M'), name, d.get('ready'), d.get('reason'),
                                   Path(str(d.get('save'))).name if d.get('save') else 'none', d.get('turn')))
                run(py + ['tools/queue/ticketq.py', 'post', '0591', '--by', 'dispatcher-B', '--kind', 'note',
                          '--text', scrub(text)], '7-autosave-posted')
            elif step == 'stop':
                run(py + ['tools/harness/cli.py', '-i', name, 'stop', '--human-turn-hook',
                          'tools.harness.turns:stop_readiness'], '8-stop', timeout=1200)
                run(py + ['tools/harness/cli.py', '-i', name, 'leftovers'], '8-leftovers', ok=(0, 1))
            elif step == 'original':
                run(py + ['tools/instances/provision.py', 'set-proxy', name, '--original', '--runs-to', a.runs],
                    '9-set-proxy-original')
            elif step == 'restore':
                r = frontend.restore(inst)
                (ev / '9-frontend-restore.json').write_text(scrub(json.dumps(r, indent=1, default=str)),
                                                            encoding='utf-8')
                pf = ev / '2-frontend-pre-prepare.json'
                if pf.is_file():
                    pre = json.loads(pf.read_text(encoding='utf-8'))
                    fe = frontend._paths(inst)[3]
                    post = {n: sha256(fe / n) for n in pre if (fe / n).is_file()}
                    same = post == pre
                    say('  front-end files after restore equal the pre-prepare hashes: %s' % same)
                    if not same:
                        raise Fail('front-end files differ from before prepare: %s vs %s' % (post, pre))
                run(py + ['tools/instances/provision.py', 'verify', name], '9-verify-after', timeout=3600)
        say('STEPS %s..%s PASSED' % (STEPS[start], STEPS[until]))
        return 0
    except Fail as e:
        say('FAILED at step %s: %s' % (step, e))
        return 1


if __name__ == '__main__':
    sys.exit(main())
