"""Tests of the server's fight clock (server/src/units/srv_units_fights.cpp; epic 0042, issue 4782, 2026-11-20).
Standalone build (the clock depends on srv_feed.h only). Pattern of run_units_records_test.py.

    set C5_JOB_OWNER=0042
    python -I perf/quietlock/quietlock.py run --who w2-0042 --what "4782 fights tests" -- \
        python -I server/tests/units/run_units_fights_test.py --out DIR [--asan]

1. clang-cl x64 (/std:c++14 /W4 /WX /O2 /MD) builds test_units_fights.cpp with srv_units_fights.cpp into
   DIR/test_units_fights.exe and runs it: every check must pass (exit 0, " 0 failed").
2. Negative: the build with -DFIGHTS_TEST_INJECT_FAIL must exit 1 with "FAIL injected" and " 1 failed".
3. --asan: MSVC cl /fsanitize=address builds a canary (must be caught) and the tests (0 reports, exit 0), under
   shell/tools/memcap.py (2048 MB job cap).
Exit 0 when every part passes, 1 when one fails, 2 when a build fails. Python standard library.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = [REPO / 'server' / 'src' / 'units' / 'srv_units_fights.cpp', HERE / 'test_units_fights.cpp']
INC = ['/I' + str(REPO / 'server' / 'src'), '/I' + str(REPO / 'server' / 'src' / 'units')]
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''
NOWIN = 0x08000000  # CREATE_NO_WINDOW


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)


def clang_build(x64build, env, out, exe, extra):
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + INC + extra + [f'/Fe{out / exe}', f'/Fo{out}\\'] + \
          [str(s) for s in SRC] + ['/link', 'kernel32.lib']
    cp = run(cmd, env=env, cwd=str(out))
    print(f'compile {exe}: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-4000:])
    return cp.returncode == 0 and (out / exe).exists()


def vcvars_env():
    cp = subprocess.run(f'call "{VCVARSALL}" x64 >nul && set', shell=True, capture_output=True, text=True,
                        env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if cp.returncode:
        return None
    env = {}
    for line in cp.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def asan(out):
    env = vcvars_env()
    if not env:
        print('run_units_fights_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_units_fights_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/WX', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS'] + INC
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_units_fights_asan.exe', SRC, flags)):
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out) + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))]
        cp = run(cmd, cwd=str(out), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_units_fights_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = run(memcap + [str(out / 'test_units_fights_asan.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    ok = cp.returncode == 0 and ' 0 failed' in text and 'AddressSanitizer' not in text
    print(text.rstrip()[-1500:])
    print(f'asan tests: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the fight clock tests (server/tests/units/run_units_fights_test.py)', None)
    except ImportError:
        print('run_units_fights_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not clang_build(x64build, env, out, 'test_units_fights.exe', []):
        print('run_units_fights_test: BUILD FAILED')
        return 2
    cp = run([str(out / 'test_units_fights.exe')])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_units_fights.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    if not clang_build(x64build, env, out, 'test_units_fights_inject.exe', ['/DFIGHTS_TEST_INJECT_FAIL']):
        print('run_units_fights_test: BUILD FAILED (inject)')
        return 2
    cp = run([str(out / 'test_units_fights_inject.exe')])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_units_fights_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    fails += not neg
    if a.asan:
        r = asan(out)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_units_fights_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
