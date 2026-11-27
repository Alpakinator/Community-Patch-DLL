"""Unit tests of the server's extras module and its first extra, VisibleAreaOutline (ticket 4938, epic 0454: the
contribution guide's worked example; research/05-graphics-composition/extras.md section X10).

    python -I perf/quietlock/quietlock.py run --who <id> --what "extras tests" -- \
        python -I server/tests/extras/run_extras_test.py --out DIR [--asan]

Every program is built into DIR (emptied first):
  1. test_extras.exe (clang-cl x64 /std:c++14 /W4 /WX, the server's toolchain): server/tests/extras/test_extras.cpp with
     server/src/extras/*.cpp, render/src/heightfield/heightfield.cpp, render/src/common/status.cpp and
     options/src/vp64_options.cpp. Its GPU parts draw on a WARP device (no adapter, no window).
  2. The runner's own negative: the test built with -DEXTRAS_TEST_INJECT_FAIL must exit 1.
  --asan: the same test under MSVC cl /fsanitize=address, after a canary that must be caught; it must pass with no
     AddressSanitizer report. Runs under shell/tools/memcap.py (2048 MB).
Exit 0 when every part passes, 1 when one fails, 2 when a build that must succeed fails. Python standard library.
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
# ticket 4934: FixRoadLayout's producer (srv_extra_roads.cpp) links the road generator and what it uses
ROADS_SRC = [
    REPO / 'render' / 'src' / 'extras' / 'roads.cpp',
    REPO / 'render' / 'src' / 'decal' / 'decal_set.cpp',
    REPO / 'render' / 'src' / 'forest' / 'cut.cpp',
    REPO / 'render' / 'src' / 'bakegrid' / 'grid.cpp',
    REPO / 'render' / 'src' / 'bakeblend' / 'blend.cpp',   # grid.cpp's slopeClass
]
MODULE_SRC = sorted((REPO / 'server' / 'src' / 'extras').glob('*.cpp')) + [
    REPO / 'render' / 'src' / 'heightfield' / 'heightfield.cpp',
    REPO / 'render' / 'src' / 'common' / 'status.cpp',
    REPO / 'options' / 'src' / 'vp64_options.cpp',
] + ROADS_SRC
SRC = MODULE_SRC + [HERE / 'test_extras.cpp']
# ticket 4934 (MC #7609): the road producer's own test, beside the farm producer's in server/tests/render/
ROADS_TEST_SRC = MODULE_SRC + [REPO / 'server' / 'src' / 'render' / 'srv_cut_tap.cpp',   # the cut overlay (MC #7664)
                                REPO / 'server' / 'tests' / 'render' / 'test_roads_producer.cpp']
INCLUDES = [REPO / 'server' / 'src', REPO / 'render' / 'include', REPO / 'options' / 'include',
            REPO / 'shell' / 'common']   # ticket 4934: srv_cut_tap.cpp's srv_render_common.h includes shell_shared.h
LIBS = ['d3d11.lib', 'dxgi.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'kernel32.lib']
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
NOWIN = 0x08000000  # CREATE_NO_WINDOW
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                          **kw)


def build_clang(x64build, env, out, exe, extra=(), srcs=None):
    obj = out / ('obj-' + Path(exe).stem)
    obj.mkdir(exist_ok=True)
    cmd = [x64build.CLANG_CL, '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + list(extra) + [f'/I{p}' for p in INCLUDES] + \
        [f'/Fe{out / exe}', f'/Fo{obj}\\'] + [str(s) for s in (srcs or SRC)] + ['/link'] + LIBS
    cp = run(cmd, env=env, cwd=str(out))
    print(f'compile {exe}: rc={cp.returncode}')
    if cp.returncode:
        print((cp.stdout + cp.stderr).strip()[-4000:])
    return cp.returncode == 0


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


def check(name, ok, detail=''):
    print(f'{"PASS" if ok else "FAIL"} {name}' + (f': {detail}' if detail and not ok else ''))
    return 0 if ok else 1


def asan(out):
    env = vcvars_env()
    if not env:
        print('run_extras_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_extras_test: ASan: cl.exe not found')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/WX', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS'] + [f'/I{p}' for p in INCLUDES]
    (out / 'asan-obj').mkdir(exist_ok=True)
    for exe, srcs, fl, libs in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                ('test_extras_asan.exe', SRC, flags, LIBS),
                                ('test_roads_producer_asan.exe', ROADS_TEST_SRC, flags, LIBS)):   # ticket 4934
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out / 'asan-obj') + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))] + (['/link'] + libs if libs else [])
        cp = run(cmd, cwd=str(out), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    for name in ('clang_rt.asan_dynamic-x86_64.dll',):
        src = Path(cl).parent / name
        if src.exists():
            shutil.copy2(src, out / name)
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = run(memcap + [out / 'asan_canary.exe'], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_extras_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = run(memcap + [out / 'test_extras_asan.exe'], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    print(text.rstrip()[-4000:])
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'run_extras_test: FAIL: ASan tests exit {cp.returncode}')
        return 1
    print('asan tests: OK (exit 0, no AddressSanitizer report)')
    cp = run(memcap + [out / 'test_roads_producer_asan.exe'], cwd=str(out), env=env)   # ticket 4934
    text = cp.stdout + cp.stderr
    print(text.rstrip()[-4000:])
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'run_extras_test: FAIL: ASan road producer tests exit {cp.returncode}')
        return 1
    print('asan road producer tests: OK (exit 0, no AddressSanitizer report)')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the extras unit tests (server/tests/extras/run_extras_test.py)', None)
    except ImportError:
        print('run_extras_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    # 1. the tests
    if not build_clang(x64build, env, out, 'test_extras.exe'):
        return 2
    cp = run([out / 'test_extras.exe'], cwd=str(out), env=env)
    print(cp.stdout.rstrip()[-6000:])
    (out / 'unit-output.txt').write_text(cp.stdout, encoding='utf-8')
    fails += check('unit tests', cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout,
                   f'exit {cp.returncode}')
    # 2. the runner's negative
    if not build_clang(x64build, env, out, 'test_extras_fail.exe', ['/DEXTRAS_TEST_INJECT_FAIL']):
        return 2
    cp = run([out / 'test_extras_fail.exe'], cwd=str(out), env=env)
    fails += check('the injected failing check fails the binary', cp.returncode == 1 and ' 1 failed' in cp.stdout,
                   f'exit {cp.returncode}')
    # 2b. ticket 4934: the road producer's test (server/tests/render/test_roads_producer.cpp)
    if not build_clang(x64build, env, out, 'test_roads_producer.exe', srcs=ROADS_TEST_SRC):
        return 2
    cp = run([out / 'test_roads_producer.exe'], cwd=str(out), env=env)
    print(cp.stdout.rstrip()[-6000:])
    (out / 'roads-output.txt').write_text(cp.stdout, encoding='utf-8')
    fails += check('road producer tests', cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout,
                   f'exit {cp.returncode}')
    # 3. ASan
    if a.asan:
        r = asan(out)
        if r == 2:
            return 2
        fails += check('ASan build of the tests', r == 0)
    print(f'run_extras_test: {"OK" if fails == 0 else "FAILED"} ({fails} part(s) failed)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
