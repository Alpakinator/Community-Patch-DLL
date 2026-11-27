"""Tests of the server's side of the bridge's ui records (issue 4754, epic 0040, 2026-11-17): SrvRemote::UiRec in
server/src/remote/srv_remote.h (decode, the kUiMirror event's layout with kind 6's player and arrival, the counters and
the report line) and the event's way through server/src/feed/srv_feed.cpp.

    python -I perf/quietlock/quietlock.py run --who <team> --what "ui record tests" -- \
        python -I server/tests/feed/run_ui_record_test.py --out DIR [--asan]

1. clang-cl x64 (the toolchain of run_feed_test.py: /std:c++14 /W4 /WX /O2 /MD) builds test_ui_record.cpp with
   srv_feed.cpp into DIR/test_ui_record.exe (srv_remote.h's ui-record block alone: SRV_REMOTE_UI_RECORD_ONLY, no DLL
   header) and runs it: every check must pass (exit 0, " 0 failed").
2. Negative test of this runner: the same build with -DFEED_TEST_INJECT_FAIL must exit 1 with a FAIL line.
3. --asan: MSVC cl (vcvarsall x64) with /fsanitize=address builds the same files and a canary that must be caught; the
   tests then pass with no AddressSanitizer report. Both under shell/tools/memcap.py (2048 MB).
Exit 0 when every part passes, 1 when one fails, 2 when a set-up or build step fails. Outputs only under DIR.
"""
import argparse
import importlib.util
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = [REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp', HERE / 'test_ui_record.cpp']
INCS = ['/I' + str(REPO / 'server' / 'src')]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def clang_build(feedrun, x64build, env, out, exe, extra):
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + INCS + extra + [f'/Fe{out / exe}', f'/Fo{out}\\'] + \
          [str(s) for s in SRC] + ['/link', 'kernel32.lib']
    cp = feedrun.run(cmd, env=env, cwd=str(out))
    print(f'compile {exe}: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-4000:])
    return cp.returncode == 0 and (out / exe).exists()


def asan(feedrun, out):
    env = feedrun.vcvars_env()
    if not env:
        print('run_ui_record_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_ui_record_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    (out / 'asan_canary.cpp').write_text(feedrun.CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS'] + INCS
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_ui_record_asan.exe', SRC, flags)):
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out) + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))]
        cp = feedrun.run(cmd, cwd=str(out), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = feedrun.run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_ui_record_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = feedrun.run(memcap + [str(out / 'test_ui_record_asan.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    print(text.rstrip()[-4000:])
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'run_ui_record_test: FAIL: ASan tests exit {cp.returncode}')
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
        gate.require('the ui record tests (server/tests/feed/run_ui_record_test.py)', None)
    except ImportError:
        print('run_ui_record_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    feedrun = load('run_feed_test', HERE / 'run_feed_test.py')
    x64build = feedrun.load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not clang_build(feedrun, x64build, env, out, 'test_ui_record.exe', []):
        print('run_ui_record_test: BUILD FAILED')
        return 2
    cp = feedrun.run([str(out / 'test_ui_record.exe')])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_ui_record.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    if not clang_build(feedrun, x64build, env, out, 'test_ui_record_inject.exe', ['/DFEED_TEST_INJECT_FAIL']):
        print('run_ui_record_test: BUILD FAILED (inject)')
        return 2
    cp = feedrun.run([str(out / 'test_ui_record_inject.exe')])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_ui_record_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    if not neg:
        print('run_ui_record_test: a failing check did not fail the binary')
        print(cp.stdout[-2000:])
    fails += not neg
    if a.asan:
        r = asan(feedrun, out)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_ui_record_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
