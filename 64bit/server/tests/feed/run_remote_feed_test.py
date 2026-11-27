"""Tests of the unit feed's DLL side on stand-in DLL objects (server/src/remote/srv_remote_feed.cpp; issue 4452, epic
0375, 2026-11-15).

    python -I perf/quietlock/quietlock.py run --who 0375 --what "unit feed tests" -- \
        python -I server/tests/feed/run_remote_feed_test.py --out DIR [--asan] [--src FORK_TREE | --fork CLONE]

1. The stand-ins: server/tests/feed/gen_standins.py writes DIR/standins.gen.h from the fork's CvDllInterfaces.h.
2. clang-cl x64 with the server's flags for server/src (server/build_server.py: /std:c++14 /W3 /WX, the DLL's predefines,
   the fork's include folders as external headers, server/src, server/src/remote, ipc/include, proxy/include) builds
   test_remote_feed.cpp with srv_remote_feed.cpp and srv_feed.cpp into DIR/test_remote_feed.exe and runs it in DIR:
   every check must pass (exit 0, "0 failed").
3. Negative test of this runner: the same build with -DFEED_TEST_INJECT_FAIL must exit 1 with a FAIL line.
4. --asan: MSVC cl (vcvarsall x64) with /fsanitize=address builds the same three files (DIR/msvc_compat.h, force-included,
   maps the clang-only attribute macros of srv_common.h to nothing) and a canary that must be caught; then the tests must
   pass with no ASan report. Both under shell/tools/memcap.py (2048 MB).
--src: a fork tree (default: the fork's include folders at deps/dll.lock's commit, `git archive` from --fork, default
tools/x64-dll/build.py DEFAULT_SRC, into DIR/src; server/tests/db/run_db_write_test.py extract_src).
Exit 0 when every part passes, 1 when one fails, 2 when a set-up or build step fails. Outputs only under DIR.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = [REPO / 'server' / 'src' / 'remote' / 'srv_remote_feed.cpp', REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp',
       HERE / 'test_remote_feed.cpp']
NOWIN = 0x08000000
COMPAT = r'''// force-included by the MSVC ASan build of the unit feed tests: srv_common.h's clang branch of the DLL's PCH macros
#pragma once
#define __attribute__(x)
#define __builtin_unreachable() __assume(0)
#define __builtin_trap() __debugbreak()
#define __builtin_assume(x) __assume(x)
'''


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--src', type=Path, default=None)
    ap.add_argument('--fork', type=Path, default=None)
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the unit feed tests (server/tests/feed/run_remote_feed_test.py)', None)
    except ImportError:
        print('run_remote_feed_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = load('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    feedrun = load('run_feed_test', HERE / 'run_feed_test.py')
    if a.src:
        src = a.src
        print(f'source {src} (--src)')
    else:
        dbtest = load('run_db_write_test', REPO / 'server' / 'tests' / 'db' / 'run_db_write_test.py')
        try:
            commit = x64build.read_dll_lock()['commit']
            dirs = [d.replace('\\', '/') for d in x64build.INCLUDE_DIRS if d != 'CvGameCoreDLL_Expansion2']
            print('source ' + dbtest.extract_src(a.fork or x64build.DEFAULT_SRC, commit, dirs, out / 'src'))
        except (OSError, ValueError, RuntimeError, KeyError) as e:
            print(f'run_remote_feed_test: set-up: {e} (pass --src, or --fork with a clone that holds the pinned commit)')
            return 2
        src = out / 'src'
    # the stand-ins of gen_standins.py's classes plus ICvMap1 (ticket 4914, epic 0615: the plot snapshot and the rolling
    # refresh read the map through ICvMap1::PlotNum / GetPlot); gen_standins.py itself is unchanged
    standins = load('gen_standins', HERE / 'gen_standins.py')
    standins.CLASSES = list(standins.CLASSES) + ['ICvMap1']
    try:
        (out / 'standins.gen.h').write_text(standins.generate(src), encoding='utf-8')
    except (OSError, RuntimeError) as e:
        print(f'gen_standins: {e}')
        return 2
    print(f'gen_standins: {out / "standins.gen.h"}: {len(standins.CLASSES)} classes')
    defs = [f'/D{d}' for d in x64build.PREDEFS['debug'] if d not in ('_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS',
                                                                      'VPDEBUG')]
    defs += ['/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING']   # srv_common.h defines the stdext hash one itself
    incs = [f'/I{out}', f'/I{REPO / "server" / "src"}', f'/I{REPO / "server" / "src" / "remote"}',
            f'/I{REPO / "ipc" / "include"}', f'/I{REPO / "proxy" / "include"}']
    ext = [f'/I{src / d}' for d in x64build.INCLUDE_DIRS]
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0

    def clang(exe, extra):
        cmd = [str(x64build.CLANG_CL), '-m64', '-msse3', '/nologo', '/MD', '/EHsc', '/fp:precise', '/Zc:wchar_t',
               '/std:c++14', '/W3', '/WX', '/O2', '/Zi', '-fuse-ld=lld'] + defs + extra + incs + \
              ['/external:W0'] + [x.replace('/I', '/external:I', 1) for x in ext] + \
              [f'-Wno-{w}' for w in x64build.CL_SUPPRESS] + \
              ['-Wno-deprecated-declarations', '-Wno-microsoft-include', '-Wno-pragma-pack', '-Wno-unused-value',
               '-Wno-overloaded-virtual', '-Wno-invalid-offsetof'] + \
              [f'/Fe{out / exe}', f'/Fo{out}\\'] + [str(s) for s in SRC] + ['/link', 'kernel32.lib']
        cp = run(cmd, env=env, cwd=str(out))
        print(f'compile {exe}: rc={cp.returncode}')
        if (cp.stdout + cp.stderr).strip():
            print((cp.stdout + cp.stderr).rstrip()[-6000:])
        return cp.returncode == 0 and (out / exe).exists()

    if not clang('test_remote_feed.exe', []):
        print('run_remote_feed_test: BUILD FAILED')
        return 2
    cp = run([str(out / 'test_remote_feed.exe')], cwd=str(out))
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_remote_feed.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    if not clang('test_remote_feed_inject.exe', ['/DFEED_TEST_INJECT_FAIL']):
        print('run_remote_feed_test: BUILD FAILED (inject)')
        return 2
    cp = run([str(out / 'test_remote_feed_inject.exe')], cwd=str(out))
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_remote_feed_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    fails += not neg
    if a.asan:
        venv = feedrun.vcvars_env()
        cl = shutil.which('cl', path=(venv or {}).get('PATH', (venv or {}).get('Path', ''))) if venv else None
        if not cl:
            print('run_remote_feed_test: ASan: vcvarsall x64 or cl.exe not found')
            return 2
        (out / 'msvc_compat.h').write_text(COMPAT, encoding='ascii')
        (out / 'asan_canary.cpp').write_text(feedrun.CANARY, encoding='ascii')
        flags = ['/nologo', '/EHsc', '/W3', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
                 '/Zc:wchar_t', '/FImsvc_compat.h', '/wd4005', '/wd4996', '/wd4068', '/wd5051'] + defs + incs + \
                ['/external:W0'] + [x.replace('/I', '/external:I', 1) for x in ext]
        for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                              ('test_remote_feed_asan.exe', SRC, flags)):
            cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out) + '\\',
                                                          '/Fd' + str(out / (exe + '.pdb'))]
            cp = run(cmd, cwd=str(out), env=venv)
            print(f'asan compile {exe}: rc={cp.returncode}')
            if cp.returncode:
                print((cp.stdout + cp.stderr)[-6000:])
                return 2
        memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
        cp = run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=venv)
        if cp.returncode == 0 or 'heap-buffer-overflow' not in cp.stdout + cp.stderr:
            print(f'run_remote_feed_test: FAIL: ASan not active (canary exit {cp.returncode})')
            return 1
        print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
        cp = run(memcap + [str(out / 'test_remote_feed_asan.exe')], cwd=str(out), env=venv)
        text = cp.stdout + cp.stderr
        print(text.rstrip()[-4000:])
        if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
            print(f'run_remote_feed_test: FAIL: ASan tests exit {cp.returncode}')
            fails += 1
        else:
            print('asan tests: OK (exit 0, no AddressSanitizer report)')
    print(f'run_remote_feed_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
