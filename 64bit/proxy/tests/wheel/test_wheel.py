"""Offline test of [Extras] FixSmoothWheel (ticket 0755): proxy/src/proxy_wheel.cpp + wheel_test.cpp against a fake EXE
image and a scripted message queue. No game is started; nothing of the EXE is read. Written after
proxy/tests/minimap_fog/test_minimap_fog.py (ticket 0727), the same steps with this ticket's names.

    python -I proxy/tests/wheel/test_wheel.py --scratch <empty or new dir> [--no-asan] [-v]

1. proxy    built with the proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9 headers, /MT), run, then run
            with --expect-fail (the program's own failure path: exit 1, one FAIL line, the summary one short).
2. asan     the same under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), run under
            shell/tools/memcap.py.
Exit code 0 when every check passes. Outputs only under --scratch. Python 3 standard library only.
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
EXEBUILD_INCLUDE = REPO / 'exebuild' / 'include'
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
SOURCES = [HERE / 'wheel_test.cpp', SRC / 'proxy_wheel.cpp']
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def run_unit(argv, label, cwd, env=None, expect_fail=False):
    cp = subprocess.run([str(a) for a in argv] + (['--expect-fail'] if expect_fail else []) + (['-v'] if VERBOSE else []),
                        capture_output=True, text=True, errors='replace', cwd=str(cwd), env=env, timeout=600)
    out = cp.stdout + cp.stderr
    m = re.search(r'wheel_test: (\d+)/(\d+) checks passed', out)
    if expect_fail:
        return check('%s (negative, --expect-fail): exit code 1, one FAIL line, the summary one short' % label,
                     cp.returncode == 1 and m and int(m.group(2)) - int(m.group(1)) == 1 and out.count('FAIL line') == 1,
                     'exit %d; %s' % (cp.returncode, out[-1500:]))
    for ln in out.splitlines():
        if ln.startswith('FAIL') or ln.startswith('  ') or VERBOSE:
            print('    ' + ln)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % cp.returncode),
                 cp.returncode == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-2000:])


def test_proxy_toolchain(s):
    d = s / 'proxy'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'wheel_test.exe'
    cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld'] +
                       ['/I' + str(SRC), '/I' + str(EXEBUILD_INCLUDE)] + [str(x) for x in SOURCES] + ['/Fe' + str(exe)],
                       cwd=str(d))
    if not check('proxy toolchain: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 (cp.stdout + cp.stderr)[-3000:]):
        return
    run_unit([exe], 'proxy toolchain', d)
    run_unit([exe], 'proxy toolchain', d, expect_fail=True)


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
    exe = d / 'wheel_test.exe'
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS',
             '/I' + str(SRC), '/I' + str(EXEBUILD_INCLUDE)]
    cp = subprocess.run([cl] + flags + [str(x) for x in SOURCES] + ['/Fe' + str(exe), '/Fo' + str(d) + '\\',
                                                                     '/Fd' + str(d / 'wheel_test.pdb')],
                        cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
    if not check('asan: wheel_test compiled with MSVC /fsanitize=address', cp.returncode == 0,
                 (cp.stdout + cp.stderr)[-2000:]):
        return
    run_unit(memcap + [exe], 'asan', d, env=env)


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
    else:
        print('note: the ASan run NOT made (--no-asan)')
    bad = [n for n, ok in RESULTS if not ok]
    print('test_wheel: %d/%d passed%s' % (len(RESULTS) - len(bad), len(RESULTS), ' (ASan NOT run)' if a.no_asan else ''))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
