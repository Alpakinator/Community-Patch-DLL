"""Tests of the per-unit hold and its view (server/src/units/srv_units_queue.cpp; epic 0042, issue 4774, 2026-11-20).
Standalone build (the hold depends on c5r/units/unit_scene.h only). The steps of run_units_fights_test.py, whose functions
this runner calls with its own sources and names.

    set C5_JOB_OWNER=0042
    python -I perf/quietlock/quietlock.py run --who w11-0042 --what "4774 queue tests" -- \
        python -I server/tests/units/run_units_queue_test.py --out DIR [--asan]

1. clang-cl x64 (/std:c++14 /W4 /WX /O2 /MD) builds test_units_queue.cpp with srv_units_queue.cpp into
   DIR/test_units_queue.exe and runs it: every check must pass (exit 0, " 0 failed").
2. Negative: the build with -DQUEUE_TEST_INJECT_FAIL must exit 1 with "FAIL injected" and " 1 failed".
3. --asan: MSVC cl /fsanitize=address builds a canary (must be caught) and the tests (0 reports, exit 0), under
   shell/tools/memcap.py (2048 MB job cap).
Exit 0 when every part passes, 1 when one fails, 2 when a build fails. Python standard library.
"""
import argparse
import importlib.util
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def load_fights():
    spec = importlib.util.spec_from_file_location('run_units_fights_test', HERE / 'run_units_fights_test.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.SRC = [REPO / 'server' / 'src' / 'units' / 'srv_units_queue.cpp', HERE / 'test_units_queue.cpp']
    mod.INC = mod.INC + ['/I' + str(REPO / 'render' / 'include')]
    return mod


def asan(f, out):
    env = f.vcvars_env()
    if not env:
        print('run_units_queue_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_units_queue_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    (out / 'asan_canary.cpp').write_text(f.CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/WX', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS'] + f.INC
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_units_queue_asan.exe', f.SRC, flags)):
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out) + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))]
        cp = f.run(cmd, cwd=str(out), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = f.run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_units_queue_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = f.run(memcap + [str(out / 'test_units_queue_asan.exe')], cwd=str(out), env=env)
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
        gate.require('the per-unit hold tests (server/tests/units/run_units_queue_test.py)', None)
    except ImportError:
        print('run_units_queue_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    f = load_fights()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = f.load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not f.clang_build(x64build, env, out, 'test_units_queue.exe', []):
        print('run_units_queue_test: BUILD FAILED')
        return 2
    cp = f.run([str(out / 'test_units_queue.exe')])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_units_queue.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    if not f.clang_build(x64build, env, out, 'test_units_queue_inject.exe', ['/DQUEUE_TEST_INJECT_FAIL']):
        print('run_units_queue_test: BUILD FAILED (inject)')
        return 2
    cp = f.run([str(out / 'test_units_queue_inject.exe')])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_units_queue_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    fails += not neg
    if a.asan:
        r = asan(f, out)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_units_queue_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
