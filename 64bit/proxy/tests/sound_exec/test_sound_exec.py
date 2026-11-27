"""Offline test of the shell's sound executor (ticket 4723, route S): proxy/src/proxy_sound_exec.cpp's core +
sound_exec_test.cpp against a stand-in trigger section (shell/common/vp_trigger.h) and a mock of the EXE's audio. Nothing
is played; no game is started; nothing of the EXE is read.

    python -I proxy/tests/sound_exec/test_sound_exec.py --scratch <empty or new dir> [--no-asan] [--log TRIGGER_LOG ...] [-v]

1. core   built with VP_SOUND_EXEC_OFFLINE on the proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9
          headers, /MT), run, then run with --expect-fail (the program's own failure path: exit 1, one FAIL line, the
          summary one short). Each --log (a recorded trigger log of 4721's text form, kept outside git: names come from
          the player's install) is replayed through the section by a further run.
2. abi    the stack effect of the binding's EXE call types (proxy_sound_exec.h): each typedef called on a stand-in
          callee written in assembly with the EXE function's own `ret N` (exe-audio.md 8), ESP compared before and after
          (the 4725 defect class: a declared signature that differs from the callee's).
3. asan   1 under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), under
          shell/tools/memcap.py.
Exit code 0 when every check passes. Outputs only under --scratch. Python 3 standard library only.
Hand-written 2026-11-20 for this repository (GPL-3.0-or-later), after proxy/tests/combat_hold/test_combat_hold.py.
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
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
SOURCES = [HERE / 'sound_exec_test.cpp', SRC / 'proxy_sound_exec.cpp']
ABI_SOURCES = [HERE / 'sound_exec_abi_test.cpp']
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def run_unit(argv, label, cwd, env=None, expect_fail=False, summary=r'sound_exec_test: (\d+)/(\d+) checks passed'):
    cp = subprocess.run([str(a) for a in argv] + (['--expect-fail'] if expect_fail else []) + (['-v'] if VERBOSE else []),
                        capture_output=True, text=True, errors='replace', cwd=str(cwd), env=env, timeout=600)
    out = cp.stdout + cp.stderr
    m = re.search(summary, out)
    if expect_fail:
        return check('%s (negative, --expect-fail): exit code 1, one FAIL line, the summary one short' % label,
                     cp.returncode == 1 and m and int(m.group(2)) - int(m.group(1)) == 1 and out.count('FAIL line') == 1,
                     'exit %d; %s' % (cp.returncode, out[-1500:]))
    for ln in out.splitlines():
        if ln.startswith('FAIL') or ln.startswith('--log') or VERBOSE:
            print('    ' + ln)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % cp.returncode),
                 cp.returncode == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-2000:])


def build_clang(d, exe, sources, defs):
    cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld'] +
                       ['/D' + x for x in defs] + ['/I' + str(SRC), '/I' + str(COMMON)] + [str(x) for x in sources] +
                       ['/Fe' + str(exe)], cwd=str(d))
    return cp


def test_proxy_toolchain(s, logs):
    d = s / 'proxy-core'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'sound_exec_test.exe'
    cp = build_clang(d, exe, SOURCES, ['VP_SOUND_EXEC_OFFLINE'])
    if not check('core, proxy toolchain: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 (cp.stdout + cp.stderr)[-3000:]):
        return
    run_unit([exe], 'core, proxy toolchain', d)
    run_unit([exe], 'core, proxy toolchain', d, expect_fail=True)
    for lg in logs:
        run_unit([exe, '--log', str(Path(lg).resolve())], 'core, proxy toolchain, --log %s' % Path(lg).name, d)
    # the abi probe: the binding's call types against callees with the EXE functions' own returns
    a = s / 'abi'
    a.mkdir(parents=True, exist_ok=True)
    aexe = a / 'sound_exec_abi_test.exe'
    cp = build_clang(a, aexe, ABI_SOURCES, [])
    if check('abi, proxy toolchain: built', cp.returncode == 0 and aexe.exists(), (cp.stdout + cp.stderr)[-3000:]):
        run_unit([aexe], 'abi, proxy toolchain', a, summary=r'sound_exec_abi_test: (\d+)/(\d+) checks passed')
        run_unit([aexe], 'abi, proxy toolchain', a, expect_fail=True,
                 summary=r'sound_exec_abi_test: (\d+)/(\d+) checks passed')


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


def test_asan(s, logs):
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
    exe = o / 'sound_exec_test.exe'
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS',
             '/DVP_SOUND_EXEC_OFFLINE', '/I' + str(SRC), '/I' + str(COMMON)]
    cp = subprocess.run([cl] + flags + [str(x) for x in SOURCES] + ['/Fe' + str(exe), '/Fo' + str(o) + '\\',
                                                                     '/Fd' + str(o / 'sound_exec_test.pdb')],
                        cwd=str(o), env=env, capture_output=True, text=True, errors='replace')
    if not check('core, asan: compiled with MSVC /fsanitize=address', cp.returncode == 0, (cp.stdout + cp.stderr)[-2000:]):
        return
    run_unit(memcap + [exe], 'core, asan', o, env=env)
    for lg in logs:
        run_unit(memcap + [exe, '--log', str(Path(lg).resolve())], 'core, asan, --log %s' % Path(lg).name, o, env=env)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('--log', action='append', default=[], help='a recorded trigger log to replay (repeatable)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    test_proxy_toolchain(s, a.log)
    if not a.no_asan:
        test_asan(s, a.log)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_sound_exec: %d/%d passed' % (len(RESULTS) - len(bad), len(RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
