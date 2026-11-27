"""Unit tests of the renderer's event feed (server/src/feed/srv_feed.cpp; issue 3303, epic 3300, 2026-11-13).

    python -I perf/quietlock/quietlock.py run --who 3300-w3 --what "feed unit tests" -- \
        python -I server/tests/feed/run_feed_test.py --out DIR [--asan]

1. clang-cl x64 (the server's toolchain, tools/x64-dll/build.py: /std:c++14 /W4 /WX /O2 /MD) builds
   server/tests/feed/test_feed.cpp with srv_feed.cpp into DIR/test_feed.exe and runs it: every check must pass (exit 0,
   "0 failed").
2. Negative test of this runner: the same build with -DFEED_TEST_INJECT_FAIL (one failing check) must exit 1 and print
   a FAIL line; otherwise the runner fails ("a failing check did not fail the binary").
3. --asan: MSVC cl (vcvarsall x64; the clang-cl ASan runtime of LLVM 19 crashes at start-up here, render/README.md)
   builds the tests with /fsanitize=address /Zi /Oy- /W4 /WX into DIR/test_feed_asan.exe and a canary (one byte past a
   heap block); the canary must be caught (heap-buffer-overflow), then the tests must pass with no ASan report. Both
   run under shell/tools/memcap.py (2048 MB job cap).
Exit 0 when every part passes, 1 when one fails, 2 when a build fails. Python standard library.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = [REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp', REPO / 'server' / 'src' / 'feed' / 'srv_feed_dbcopy.cpp',
       HERE / 'test_feed.cpp']   # srv_feed_dbcopy.cpp: issue 4452
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
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + extra + [f'/Fe{out / exe}', f'/Fo{out}\\'] + \
          [str(s) for s in SRC] + ['/link', 'kernel32.lib']
    cp = run(cmd, env=env, cwd=str(out))
    print(f'compile {exe}: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-4000:])
    return cp.returncode == 0 and (out / exe).exists()


def vcvars_env():
    cp = subprocess.run(f'call "{VCVARSALL}" x64 >nul && set', shell=True, capture_output=True, text=True,
                        env=dict(__import__('os').environ, VSCMD_SKIP_SENDTELEMETRY='1'))
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
        print('run_feed_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_feed_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    # /W4 without /WX here: the clang-cl build above is the warning gate (/W4 /WX); MSVC's own warnings are printed
    flags = ['/nologo', '/EHsc', '/W4', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS']
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_feed_asan.exe', SRC, flags)):
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
        print(f'run_feed_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = run(memcap + [str(out / 'test_feed_asan.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    print(text.rstrip()[-4000:])
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'run_feed_test: FAIL: ASan tests exit {cp.returncode}')
        return 1
    print('asan tests: OK (exit 0, no AddressSanitizer report)')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the feed unit tests (server/tests/feed/run_feed_test.py)', None)
    except ImportError:
        print('run_feed_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not clang_build(x64build, env, out, 'test_feed.exe', []):
        print('run_feed_test: BUILD FAILED')
        return 2
    cp = run([str(out / 'test_feed.exe')])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_feed.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    # negative: an injected failing check must fail the binary
    if not clang_build(x64build, env, out, 'test_feed_inject.exe', ['/DFEED_TEST_INJECT_FAIL']):
        print('run_feed_test: BUILD FAILED (inject)')
        return 2
    cp = run([str(out / 'test_feed_inject.exe')])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_feed_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    if not neg:
        print('run_feed_test: a failing check did not fail the binary')
        print(cp.stdout[-2000:])
    fails += not neg
    if a.asan:
        r = asan(out)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_feed_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
