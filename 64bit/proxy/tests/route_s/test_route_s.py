"""Offline test of route S's common state (ticket 4783, MC ruling #8543 point 4): proxy/src/proxy_route_s.cpp's core with
the three real route S cores it governs (proxy_combat_hold.cpp, proxy_sound_exec.cpp, proxy_effect_player.cpp) +
route_s_test.cpp, against a fake queue B, a stand-in trigger section (shell/common/vp_trigger.h R9), a mock audio, a mock
poster and one DWORD as the app's DisableSystems mask. Nothing is played; no game is started; nothing of the EXE is read.

    python -I proxy/tests/route_s/test_route_s.py --scratch <empty or new dir> [--no-asan] [-v]

1. core   built with VP_ROUTE_S_OFFLINE, VP_COMBAT_HOLD_OFFLINE, VP_SOUND_EXEC_OFFLINE and VP_EFFECT_PLAYER_OFFLINE on the
          proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9 headers, /MT), run, then run with --expect-fail
          (the program's own failure path: exit 1, one FAIL line, the summary one short).
2. asan   1 under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), under
          shell/tools/memcap.py.
Exit code 0 when every check passes. Outputs only under --scratch. Python 3 standard library only.
Hand-written 2026-11-20 for this repository (GPL-3.0-or-later), after proxy/tests/sound_exec/test_sound_exec.py.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / 'proxy' / 'gen'))
import toolchain as tc  # noqa: E402

SRC = REPO / 'proxy' / 'src'
COMMON = REPO / 'shell' / 'common'
EXEBUILD_INCLUDE = REPO / 'exebuild' / 'include'
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
SOURCES = [HERE / 'route_s_test.cpp', SRC / 'proxy_route_s.cpp', SRC / 'proxy_combat_hold.cpp',
           SRC / 'proxy_sound_exec.cpp', SRC / 'proxy_effect_player.cpp']
DEFS = ['VP_ROUTE_S_OFFLINE', 'VP_COMBAT_HOLD_OFFLINE', 'VP_SOUND_EXEC_OFFLINE', 'VP_EFFECT_PLAYER_OFFLINE']
INCS = [SRC, COMMON, EXEBUILD_INCLUDE]
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def run_unit(argv, label, cwd, env=None, expect_fail=False, summary=r'route_s_test: (\d+)/(\d+) checks passed'):
    cp = subprocess.run([str(a) for a in argv] + (['--expect-fail'] if expect_fail else []) + (['-v'] if VERBOSE else []),
                        capture_output=True, text=True, errors='replace', cwd=str(cwd), env=env, timeout=600)
    out = cp.stdout + cp.stderr
    m = re.search(summary, out)
    if expect_fail:
        return check('%s (negative, --expect-fail): exit code 1, one FAIL line, the summary one short' % label,
                     cp.returncode == 1 and m and int(m.group(2)) - int(m.group(1)) == 1 and out.count('FAIL line') == 1,
                     'exit %d; %s' % (cp.returncode, out[-1500:]))
    for ln in out.splitlines():
        if ln.startswith('FAIL') or VERBOSE:
            print('    ' + ln)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % cp.returncode),
                 cp.returncode == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-2000:])


def test_proxy_toolchain(s):
    d = s / 'proxy-core'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'route_s_test.exe'
    cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld'] +
                       ['/D' + x for x in DEFS] + ['/I' + str(i) for i in INCS] + [str(x) for x in SOURCES] +
                       ['/Fe' + str(exe)], cwd=str(d))
    if not check('core, proxy toolchain: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 (cp.stdout + cp.stderr)[-3000:]):
        return
    run_unit([exe], 'core, proxy toolchain', d)
    run_unit([exe], 'core, proxy toolchain', d, expect_fail=True)


def vcvars_env(arch):
    out = subprocess.run('call "%s" %s >nul && set' % (VCVARSALL, arch), shell=True, capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if out.returncode:
        return None
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def test_asan(s):
    env = vcvars_env('x86')
    if not check('asan: vcvarsall x86', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    cp = subprocess.run([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', str(d / 'canary.cpp'), '/Fe' + str(d / 'canary.exe'),
                         '/Fo' + str(d) + '\\', '/Fd' + str(d / 'canary.pdb')], cwd=str(d), env=env, capture_output=True,
                        text=True, errors='replace')
    if not check('asan: canary.exe compiled with MSVC /fsanitize=address', cp.returncode == 0, (cp.stdout + cp.stderr)[-2000:]):
        return
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = subprocess.run(memcap + [str(d / 'canary.exe')], cwd=str(d), env=env, capture_output=True, text=True,
                        errors='replace')
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          cp.returncode != 0 and 'heap-buffer-overflow' in cp.stdout + cp.stderr, (cp.stdout + cp.stderr)[-800:])
    o = d / 'core'
    o.mkdir(parents=True, exist_ok=True)
    exe = o / 'route_s_test.exe'
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS'] + \
        ['/D' + x for x in DEFS] + ['/I' + str(i) for i in INCS]
    cp = subprocess.run([cl] + flags + [str(x) for x in SOURCES] + ['/Fe' + str(exe), '/Fo' + str(o) + '\\',
                                                                     '/Fd' + str(o / 'route_s_test.pdb')],
                        cwd=str(o), env=env, capture_output=True, text=True, errors='replace')
    if not check('core, asan: compiled with MSVC /fsanitize=address', cp.returncode == 0, (cp.stdout + cp.stderr)[-2000:]):
        return
    run_unit(memcap + [exe], 'core, asan', o, env=env)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    test_proxy_toolchain(s)
    if not a.no_asan:
        test_asan(s)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_route_s: %d/%d passed' % (len(RESULTS) - len(bad), len(RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
