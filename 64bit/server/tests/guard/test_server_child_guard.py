"""Ticket 0475, hermetic: the server's window guard (server/src/srv_guard.cpp) follows the server's 32-bit children.

    python -I server/tests/guard/test_server_child_guard.py --out DIR [--bin NATIVE_BIN] [--asan] [-v]

The server starts the patch's D118 popup helper itself (remote/srv_exe_crash.cpp StartHelper: vp64helper.exe for the
EXE-crash and DLL-fatal reports). child_guard_unit.cpp links the real srv_guard.cpp, installs the guard as srv_main.cpp
does and starts a child exactly as StartHelper does. The child is tools/harness/native's stand_in.exe in its popup mode
(`--popup-after`: class VP64HelperPopup, WS_EX_TOPMOST, ShowWindow(SW_SHOWNORMAL) + SetForegroundWindow, as
vp64helper.exe; issue 0473), started late (the unit is already running). NATIVE_BIN (default tools/harness/native/bin,
built by build.bat) gives stand_in.exe, focus_guard.dll and bg_helper32.exe; they are copied into DIR first.
Everything runs on a hidden desktop (COMMON rule 14); a poller attached to it lists every top-level window every 20 ms.

Cases (CIVBG_GUARD=1 and C5_CHILD_GUARD=<DIR>/bin/focus_guard.dll unless said otherwise):
  guarded    the child starts (SPAWN pid), runs with its own guard (its section: guard_state 1, child_parent = the
             unit's pid); the unit's guard counts child_guard 1, seen 1, guarded 1, failed 0, last pid = the child;
             the popup exists, with its Close button (dismiss, ticket 0472, can find it), and is cloaked on every poll;
             the child's guard cloaked it at creation, refused its foreground request, no watchdog catch; the child's
             own log says cloaked=1 and active_is_me=0 on every POPUP line
  teeth      C5_CHILD_GUARD removed: the unit's guard installs (child_guard 0) and the popup is visible and uncloaked
             (hidden desktop only): the checks above can fail
  product    CIVBG_GUARD removed, C5_CHILD_GUARD kept: no guard at all ("not requested"), the child has no section
  failclosed C5_CHILD_TEST_FAIL=inject: the child is not started (SPAWN FAILED error=1114), child_failed 1, no popup,
             the child's log stays empty
  x64        a 64-bit child (the unit itself, `noop`): started, counted in child_other, not guarded
  nofile     C5_CHILD_GUARD naming a file that does not exist: the guard's self-test fails (missing 0x40), the unit
             exits 0xC5F00032 before it starts anything
  noinject   C5_CHILD_GUARD naming a guard DLL with no bg_helper32.exe beside it: the same
--asan: the unit once more as an MSVC /fsanitize=address build (x64) with a canary that must be caught; the cases
guarded, failclosed and x64 on it; no AddressSanitizer report in any run.
Exit 0 when every check passes. Outputs only under --out. Start it through perf/quietlock/quietlock.py run.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / 'proxy' / 'tools'))
sys.path.insert(0, str(REPO / 'tools' / 'harness'))
import test_proxy_rt as rt  # noqa: E402  (toolchain, hidden desktops, vcvarsall, check)
import winapi as w  # noqa: E402

check = rt.check
SRCS = [HERE / 'child_guard_unit.cpp', REPO / 'server' / 'src' / 'srv_guard.cpp']
LIBS = ['user32.lib', 'advapi32.lib']
POPUP_CLASS = 'VP64HelperPopup'
SELFTEST_EXIT = 0xC5F00032
ERROR_DLL_INIT_FAILED = 1114
DESKS = {}
RESULTS = {}


def desk():
    return DESKS.setdefault('d', rt.desktops.HiddenDesktop('C5T0475'))


def base_env(extra=None, drop=(), base=None):
    """The test's environment: no harness or guard variable of the calling shell, then the case's."""
    e = {k: v for k, v in (base or os.environ).items()
         if not k.upper().startswith(('C5_', 'CIVBG_')) and k.upper() not in ('VPPROXY_INI', 'VP64_OPTIONS')}
    e.update(extra or {})
    for k in drop:
        e.pop(k, None)
    return e


def build_clang(out, srcs):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cp = rt.tc.run_in_env('x64', [rt.tc.CLANG, '-m64', '/MD', '/O2', '/Zi', '/EHsc', '/std:c++14', '/W3', '/WX',
                                  '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '-fuse-ld=lld',
                                  '/I' + str(REPO / 'server' / 'src')] + [str(s) for s in srcs] +
                          ['/Fe' + str(out)] + LIBS, cwd=str(out.parent))
    return cp.returncode == 0 and out.exists(), cp.stdout + cp.stderr


def _alive(pid):
    h = w.kernel32.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    import ctypes
    code = ctypes.c_ulong()
    w.kernel32.GetExitCodeProcess(h, ctypes.byref(code))
    w.kernel32.CloseHandle(h)
    return code.value == 259   # STILL_ACTIVE


def _buttons(hwnd):
    out = []
    cb = w.WNDENUMPROC(lambda h, _: out.append((w.window_class(h), w.window_text(h))) or True)
    w.user32.EnumChildWindows(hwnd, cb, 0)
    return [t for c, t in out if c.lower() == 'button']


def _guard_log(g):
    return bytes(g.mm[1284:1544]).split(b'\0', 1)[0].decode('mbcs', 'replace')


def run_case(s, unit, name, child_cmd, env, hold=9.0, watch_s=10.0):
    """Starts the unit on the hidden desktop and polls that desktop every 20 ms until watch_s or the unit's end."""
    d = s / name
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    out = d / 'unit.txt'
    r = {'name': name, 'child_cmd': child_cmd, 'polls': 0, 'popup_polls': 0, 'popup_uncloaked_visible': 0,
         'other_visible': {}, 'popup': None, 'buttons': None, 'child_pid': None, 'child_guard': None,
         'unit_children': None, 'unit_log': None, 'child_log_path': None}

    def body():
        p = rt.OnDesktop(desk().name, [str(unit), 'spawn', child_cmd, '--hold', str(hold)], env, out, cwd=d)
        r['unit_pid'] = p.pid
        t0 = time.monotonic()
        while time.monotonic() - t0 < watch_s:
            r['polls'] += 1
            txt = rt.text(out)
            if r['child_pid'] is None:
                m = re.search(r'SPAWN pid=(\d+)', txt)
                if m:
                    r['child_pid'] = int(m.group(1))
            for hwnd, wpid in w.top_windows_by_pid():
                if wpid not in (r['child_pid'], p.pid):
                    continue
                cls = w.window_class(hwnd)
                vis, cloaked = bool(w.user32.IsWindowVisible(hwnd)), w.is_cloaked(hwnd)
                if cls == POPUP_CLASS and wpid == r['child_pid']:
                    r['popup_polls'] += 1
                    if r['popup'] is None:
                        r['popup'] = {'title': w.window_text(hwnd), 'first_poll': r['polls'],
                                      't_s': round(time.monotonic() - t0, 3)}
                    if not r['buttons']:
                        r['buttons'] = _buttons(hwnd)
                    if vis and not cloaked:
                        r['popup_uncloaked_visible'] += 1
                elif vis and not cloaked:
                    r['other_visible'][cls] = r['other_visible'].get(cls, 0) + 1
            if r['child_pid']:
                with w.Guard(r['child_pid']) as g:
                    if g.present and g.read_i32(1276) != 0:
                        cg = r['child_guard'] or {}
                        cg.update(g.children(), guard_state=g.read_i32(1276), guard_missing=g.read_i32(1280),
                                  win_created=g.read_i32(1244), win_cloaked_create=g.read_i32(1248),
                                  win_cloaked_watchdog=g.read_i32(1256), win_fg_blocked=g.read_i32(1264),
                                  counters=g.counters(), cloak=g.read_i32(920))
                        r['child_guard'] = cg
                        if not r['child_log_path']:
                            r['child_log_path'] = _guard_log(g)
            with w.Guard(p.pid) as g:
                if g.present:
                    r['unit_children'] = g.children()
                    r['unit_status'] = g.status()
                    r['unit_log'] = _guard_log(g)
            if 'END' in txt or (time.monotonic() - t0 > 1.0 and not _alive(p.pid)):
                break
            time.sleep(0.02)
        r['rc'] = p.wait(hold + 30)
        r['out'] = rt.text(out)

    desk().run(body, timeout=hold + 120)
    if r['child_pid']:
        for _ in range(100):   # the child ends by --exit-after; its log may be written a moment longer
            if not _alive(r['child_pid']):
                break
            time.sleep(0.1)
        r['child_alive_at_end'] = _alive(r['child_pid'])
    for key, dst in (('unit_log', 'unit-guard.log'), ('child_log_path', 'child-guard.log')):
        if r.get(key) and os.path.isfile(r[key]):
            shutil.copy2(r[key], d / dst)
    r['rc'] = None if r['rc'] is None else r['rc'] & 0xFFFFFFFF
    RESULTS[name] = {k: v for k, v in r.items() if k not in ('unit_log', 'child_log_path')}
    return r


def child_text(d):
    return rt.text(d / 'child.log') if (d / 'child.log').exists() else ''


def popup_cmd(b, d, popup_after=1.5, exit_after=6):
    return '%s --popup-after %s --exit-after %s --log %s' % (b / 'stand_in.exe', popup_after, exit_after, d / 'child.log')


def cases(s, unit, b, tag, which=None):
    guard_dll = b / 'focus_guard.dll'
    on = base_env({'CIVBG_GUARD': '1', w.CHILD_GUARD_ENV: str(guard_dll)})

    def want(n):
        return which is None or n in which

    if want('guarded'):
        n = tag + '-guarded'
        r = run_case(s, unit, n, popup_cmd(b, s / n), on)
        out = r['out']
        check('%s guarded: the unit installed its guard with children (GUARD state=1, CreateProcessInternalW:ok)' % tag,
              'GUARD state=1 ' in out and 'CreateProcessInternalW:ok' in out, out[:1500])
        check('%s guarded: the child started (SPAWN pid)' % tag, r['child_pid'] is not None, out[-1500:])
        uc = r['unit_children'] or {}
        check('%s guarded: the unit\'s guard: child_guard 1, seen 1, guarded 1, other 0, failed 0, last pid = the child'
              % tag, uc.get('child_guard') == 1 and uc.get('child_seen') == 1 and uc.get('child_guarded') == 1 and
              uc.get('child_other') == 0 and uc.get('child_failed') == 0 and uc.get('child_last_pid') == r['child_pid'],
              json.dumps(uc))
        cg = r['child_guard'] or {}
        check('%s guarded: the child runs with its own guard: guard_state 1, child_parent = the unit, cloak 1' % tag,
              cg.get('guard_state') == 1 and cg.get('child_parent') == r.get('unit_pid') and cg.get('cloak') == 1,
              json.dumps({k: v for k, v in cg.items() if k != 'counters'}))
        check('%s guarded: the popup appeared, with its Close button (dismiss can find it)' % tag,
              r['popup'] is not None and 'Close' in (r['buttons'] or []), '%s %s' % (r['popup'], r['buttons']))
        check('%s guarded: the popup watched on at least 50 polls (20 ms), never visible and uncloaked' % tag,
              r['popup_polls'] >= 50 and r['popup_uncloaked_visible'] == 0,
              'polls %d, popup %d, uncloaked %d' % (r['polls'], r['popup_polls'], r['popup_uncloaked_visible']))
        check('%s guarded: no other window of the unit or the child visible and uncloaked' % tag, not r['other_visible'],
              json.dumps(r['other_visible']))
        check('%s guarded: the child\'s guard cloaked at creation, refused the foreground request, no watchdog catch' % tag,
              cg.get('win_cloaked_create', 0) >= 1 and cg.get('win_cloaked_watchdog', 1) == 0 and
              cg.get('counters', {}).get('SetForegroundWindow', {}).get('altered', 0) >= 1,
              json.dumps(cg.get('counters')))
        ct = child_text(s / n)
        shown = [ln for ln in ct.splitlines() if 'POPUP shown' in ln or 'POPUP tick' in ln]
        check('%s guarded: every POPUP line of the child says cloaked=1 active_is_me=0' % tag,
              shown and all('cloaked=1' in ln and 'active_is_me=0' in ln for ln in shown), '\n'.join(shown[:6]) or ct[-800:])
        check('%s guarded: the DESCRIBE line counts the child (children: guard 1, seen 1, guarded 1)' % tag,
              'children: guard 1, seen 1, guarded 1, other 0, failed 0' in out, out[-1200:])
        ul = rt.text(s / n / 'unit-guard.log') if (s / n / 'unit-guard.log').exists() else ''
        check('%s guarded: the unit\'s guard log names the child as guarded through bg_helper32' % tag,
              re.search(r'child pid %s \(.*stand_in\.exe\): guarded \(guard_state 1 \(bg_helper32 pid \d+\)' % r['child_pid'],
                        ul) is not None, ul[-1500:])

    if want('teeth'):
        n = tag + '-teeth'
        r = run_case(s, unit, n, popup_cmd(b, s / n), base_env({'CIVBG_GUARD': '1'}))
        uc = r['unit_children'] or {}
        check('%s teeth: without C5_CHILD_GUARD the guard installs (child_guard 0) and the child starts unguarded' % tag,
              'GUARD state=1 ' in r['out'] and uc.get('child_guard') == 0 and r['child_pid'] is not None and
              r['child_guard'] is None, '%s %s' % (json.dumps(uc), r['out'][-800:]))
        check('%s teeth: the popup is then visible and uncloaked (on the hidden desktop): the guarded check can fail' % tag,
              r['popup'] is not None and r['popup_uncloaked_visible'] > 0,
              'popup %s uncloaked %d' % (r['popup'], r['popup_uncloaked_visible']))

    if want('product'):
        n = tag + '-product'
        r = run_case(s, unit, n, popup_cmd(b, s / n, exit_after=3),
                     base_env({w.CHILD_GUARD_ENV: str(guard_dll)}), hold=4, watch_s=5)
        check('%s product: without CIVBG_GUARD nothing is installed ("not requested"), the child starts, has no guard' % tag,
              'GUARD state=0 not requested' in r['out'] and r['child_pid'] is not None and r['child_guard'] is None and
              r['unit_children'] is None, r['out'][-800:])

    if want('failclosed'):
        n = tag + '-failclosed'
        r = run_case(s, unit, n, popup_cmd(b, s / n), dict(on, C5_CHILD_TEST_FAIL='inject'), hold=4, watch_s=5)
        uc = r['unit_children'] or {}
        check('%s failclosed: the child is not started: SPAWN FAILED error=1114' % tag,
              'SPAWN FAILED error=%d' % ERROR_DLL_INIT_FAILED in r['out'] and r['child_pid'] is None, r['out'][-800:])
        check('%s failclosed: child_seen 1, child_failed 1, child_guarded 0' % tag,
              uc.get('child_seen') == 1 and uc.get('child_failed') == 1 and uc.get('child_guarded') == 0, json.dumps(uc))
        check('%s failclosed: no popup, and the child never ran (its log is empty)' % tag,
              r['popup'] is None and not child_text(s / n), child_text(s / n)[-400:])
        ul = rt.text(s / n / 'unit-guard.log') if (s / n / 'unit-guard.log').exists() else ''
        check('%s failclosed: the unit\'s guard log says NOT guarded ... terminated before its first instruction' % tag,
              'NOT guarded (C5_CHILD_TEST_FAIL=inject (test)): terminated before its first instruction' in ul, ul[-1200:])

    if want('x64'):
        n = tag + '-x64'
        r = run_case(s, unit, n, '%s noop' % unit, on, hold=3, watch_s=4)
        uc = r['unit_children'] or {}
        check('%s x64: a 64-bit child is started, counted in child_other, not guarded' % tag,
              r['child_pid'] is not None and uc.get('child_seen') == 1 and uc.get('child_other') == 1 and
              uc.get('child_guarded') == 0 and uc.get('child_failed') == 0, '%s %s' % (json.dumps(uc), r['out'][-600:]))

    if want('nofile'):
        n = tag + '-nofile'
        r = run_case(s, unit, n, popup_cmd(b, s / n), dict(on, **{w.CHILD_GUARD_ENV: str(b / 'no_such_guard.dll')}),
                     hold=2, watch_s=3)
        check('%s nofile: C5_CHILD_GUARD not a file: self-test fails (missing 0x40), exit 0xC5F00032, nothing started' % tag,
              r['rc'] == SELFTEST_EXIT and 'missing 0x40' in r['out'] and 'children:C5_CHILD_GUARD-not-a-file' in r['out']
              and 'SPAWN' not in r['out'], '%s %s' % (r['rc'], r['out'][-800:]))

    if want('noinject'):
        n = tag + '-noinject'
        lonely = s / n / 'lonely'
        lonely.mkdir(parents=True, exist_ok=True)
        shutil.copy2(guard_dll, lonely / 'focus_guard.dll')
        r = run_case(s, unit, n + '-run', popup_cmd(b, s / n), dict(on, **{w.CHILD_GUARD_ENV: str(lonely / 'focus_guard.dll')}),
                     hold=2, watch_s=3)
        check('%s noinject: no bg_helper32.exe beside the guard DLL: self-test fails (missing 0x40), exit 0xC5F00032' % tag,
              r['rc'] == SELFTEST_EXIT and 'missing 0x40' in r['out'] and 'children:no-bg_helper32' in r['out'] and
              'SPAWN' not in r['out'], '%s %s' % (r['rc'], r['out'][-800:]))


def test_asan(s, b):
    env = rt.vcvars_env('x64')
    if not check('asan: vcvarsall x64', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan-build'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
             '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(REPO / 'server' / 'src')]
    for exe, srcs, fl, libs in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                ('child_guard_unit.exe', SRCS, flags, LIBS)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))] + libs,
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address (x64)' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    # The ASan runtime DLL lives next to cl.exe: the unit finds it through PATH.
    asan_path = os.path.dirname(cl) + os.pathsep + os.environ.get('PATH', '')
    p = rt.OnDesktop(desk().name, [str(d / 'canary.exe')], base_env({'PATH': asan_path}), d / 'canary.txt', cwd=d)
    p.wait(60)
    out = rt.text(d / 'canary.txt')
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          'heap-buffer-overflow' in out, out[-600:])
    first = len(rt.RESULTS)
    global base_env_path
    base_env_path = asan_path
    cases(s, d / 'child_guard_unit.exe', b, 'asan', which=('guarded', 'failclosed', 'x64'))
    reports = [q for q in s.rglob('asan-*/unit.txt') if 'AddressSanitizer' in rt.text(q)]
    check('asan: no AddressSanitizer report in any run of the ASan build (%d checks above)' % (len(rt.RESULTS) - first),
          not reports, ', '.join(str(q) for q in reports[:5]))


base_env_path = None
_base_env = base_env


def base_env(extra=None, drop=(), base=None):  # noqa: F811  (the ASan runs add the runtime's folder to PATH)
    e = _base_env(extra, drop, base)
    if base_env_path:
        for k in list(e):
            if k.upper() == 'PATH':
                del e[k]
        e['PATH'] = base_env_path
    return e


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--bin', type=Path, default=REPO / 'tools' / 'harness' / 'native' / 'bin')
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    shutil.rmtree(s, ignore_errors=True)
    s.mkdir(parents=True)
    t0 = time.time()
    try:
        b = s / 'bin'
        b.mkdir()
        have = all((a.bin / f).is_file() for f in ('stand_in.exe', 'focus_guard.dll', 'bg_helper32.exe'))
        if check('native binaries (stand_in.exe, focus_guard.dll, bg_helper32.exe) in %s' % a.bin, have):
            for f in ('stand_in.exe', 'focus_guard.dll', 'bg_helper32.exe'):
                shutil.copy2(a.bin / f, b / f)
            unit = s / 'clang' / 'child_guard_unit.exe'
            ok, txt = build_clang(unit, SRCS)
            if check('build: child_guard_unit.cpp + srv_guard.cpp (clang-cl x64, /W3 /WX)', ok, txt[-2000:]):
                cases(s, unit, b, 'clang')
            if a.asan:
                test_asan(s, b)
    finally:
        for dk in DESKS.values():
            dk.close()
        (s / 'results.json').write_text(json.dumps(RESULTS, indent=1, default=str), encoding='utf-8')
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('test_server_child_guard: %d/%d checks passed (%.0f s)' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS),
                                                                    time.time() - t0))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
