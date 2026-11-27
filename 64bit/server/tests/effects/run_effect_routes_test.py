"""Tests of a fight's effect routes into the trigger stream (server/src/effects/srv_effect_routes.cpp; epic 0042, ticket
4793, 2026-11-20). Standalone build: the routes, 4722's emitter and log sink, the fight clock (srv_units_fights.cpp) and
the test, linked against a render build's c5render.lib and c5r_models.lib (the effect library, 4792's ProjectileSet,
4721's trigger log, this ticket's c5r::effects trigger intake). Pattern of run_trigger_emitter_test.py.

    set C5_JOB_OWNER=0042
    python -I perf/quietlock/quietlock.py run --who w22-0042 --what "4793 routes tests" -- \
        python -I server/tests/effects/run_effect_routes_test.py --out DIR --render-build RBUILD \
        [--asan --render-asan-build RBUILD_ASAN]

RBUILD: a tree of `render/build.py --build-dir RBUILD --target c5render` (Release, clang-cl). With --asan, RBUILD_ASAN is
a tree of `render/build.py --asan --build-dir RBUILD_ASAN --target c5render` (MSVC cl /fsanitize=address).
1. clang-cl x64 (/std:c++14 /W4 /WX /O2 /MD) builds test_effect_routes.cpp with the sources into DIR and runs it.
2. Negative: the build with -DROUTES_TEST_INJECT_FAIL must exit 1 with "FAIL injected" and " 1 failed".
3. --asan: MSVC cl /fsanitize=address builds a canary (must be caught) and the tests (0 reports, exit 0) against the
   ASan render tree, under shell/tools/memcap.py (2048 MB job cap).
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
SRC = [REPO / 'server' / 'src' / 'effects' / 'srv_effect_routes.cpp',
       REPO / 'server' / 'src' / 'effects' / 'srv_trigger_emitter.cpp',
       REPO / 'server' / 'src' / 'effects' / 'srv_trigger_logsink.cpp',
       REPO / 'server' / 'src' / 'units' / 'srv_units_fights.cpp',
       HERE / 'test_effect_routes.cpp']
INC = ['/I' + str(p) for p in (REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'render' / 'include',
                                REPO / 'models' / 'include', HERE)]
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


def libs(rbuild):
    out = [rbuild / 'c5render.lib', rbuild / 'src' / 'units' / 'c5r_models.lib']
    missing = [str(p) for p in out if not p.exists()]
    return out, missing


def clang_build(x64build, env, out, exe, extra, rlibs):
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + INC + extra + [f'/Fe{out / exe}', f'/Fo{out}\\'] + \
          [str(s) for s in SRC] + ['/link'] + [str(p) for p in rlibs] + ['kernel32.lib', 'ole32.lib', 'shell32.lib']
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


def asan(out, rbuild):
    env = vcvars_env()
    if not env:
        print('run_effect_routes_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_effect_routes_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    rlibs, missing = libs(rbuild)
    if missing:
        print(f'run_effect_routes_test: ASan: missing {missing} (render/build.py --asan --target c5render first)')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/WX', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS'] + INC
    adir = out / 'asan'
    adir.mkdir(exist_ok=True)
    for exe, srcs, fl, link in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                ('test_effect_routes_asan.exe', SRC, flags,
                                 ['/link'] + [str(p) for p in rlibs] + ['ole32.lib', 'shell32.lib'])):
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(adir / exe), '/Fo' + str(adir) + '\\',
                                                      '/Fd' + str(adir / (exe + '.pdb'))] + link
        cp = run(cmd, cwd=str(adir), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = run(memcap + [str(adir / 'asan_canary.exe')], cwd=str(adir), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_effect_routes_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = run(memcap + [str(adir / 'test_effect_routes_asan.exe'), str(adir)], cwd=str(adir), env=env)
    text = cp.stdout + cp.stderr
    ok = cp.returncode == 0 and ' 0 failed' in text and 'AddressSanitizer' not in text
    print(text.rstrip()[-2500:])
    print(f'asan tests: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--render-build', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--render-asan-build', type=Path)
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the effect routes tests (server/tests/effects/run_effect_routes_test.py)', None)
    except ImportError:
        print('run_effect_routes_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    if a.asan and not a.render_asan_build:
        print('run_effect_routes_test: --asan needs --render-asan-build')
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rlibs, missing = libs(a.render_build.resolve())
    if missing:
        print(f'run_effect_routes_test: missing {missing} (render/build.py --target c5render first)')
        return 2
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not clang_build(x64build, env, out, 'test_effect_routes.exe', [], rlibs):
        print('run_effect_routes_test: BUILD FAILED')
        return 2
    cp = run([str(out / 'test_effect_routes.exe'), str(out)])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_effect_routes.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    if not clang_build(x64build, env, out, 'test_effect_routes_inject.exe', ['/DROUTES_TEST_INJECT_FAIL'], rlibs):
        print('run_effect_routes_test: BUILD FAILED (inject)')
        return 2
    cp = run([str(out / 'test_effect_routes_inject.exe'), str(out)])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_effect_routes_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    fails += not neg
    if a.asan:
        r = asan(out, a.render_asan_build.resolve())
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_effect_routes_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
