"""Build test_pages.exe, the tests of ticket 0587's BuildPageWithWindows (server/src/render/srv_liveterrain_pages.*).
Model: server/tests/render/liveterrain/test_liveterrain_build.py.

    python -I perf/quietlock/quietlock.py run --who impl-0587 --what "0587 page tests build" -- \
        python -I server/tests/render/liveterrain/pages/test_pages_build.py --out DIR [--asan]

test_pages.exe = server/src/render/srv_liveterrain_pages.cpp, render/src/bakeheight/heightpage.cpp (bake::buildHeightPage,
the reference and the builder both use), render/src/common/status.cpp and test_pages.cpp. No producer, no c5render.
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py) into DIR/bin. --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime copied beside it (issue 0149).
Exit 0 built, 1 a step failed (last line `test_pages_build: FAILED: <why>`), 3 the launch gate refused.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]
NOWIN = 0x08000000
SRC = [REPO / 'server' / 'src' / 'render' / 'srv_liveterrain_pages.cpp', REPO / 'render' / 'src' / 'bakeheight' / 'heightpage.cpp',
       REPO / 'render' / 'src' / 'common' / 'status.cpp', HERE / 'test_pages.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'render' / 'include']
LIBS = ['d3d11.lib', 'kernel32.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'test_pages_build: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('0587 page tests build (server/tests/render/liveterrain/pages/test_pages_build.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    x64build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(x64build)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m), out / 'vcvars-env.json')
    env = dict(env, VSCMD_SKIP_SENDTELEMETRY='1')
    bdir = out / ('bin-asan' if a.asan else 'bin')
    odir = out / ('obj-asan' if a.asan else 'obj')
    bdir.mkdir(parents=True, exist_ok=True)
    odir.mkdir(parents=True, exist_ok=True)
    cl = None
    if a.asan:
        cl = shutil.which('cl.exe', path=env.get('PATH') or env.get('Path'))
        if not cl:
            fail('cl.exe not on the vcvarsall PATH')
        cc = [cl, '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/Zi', '/Oy-', '/O2', '/fsanitize=address',
              '/D_CRT_SECURE_NO_WARNINGS']
    else:
        cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
              '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS']
    cc += ['/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN']   # as render/CMakeLists.txt (heightpage.cpp calls std::min / std::max)
    exe = bdir / 'test_pages.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG'] + LIBS
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('test_pages.exe')
    if a.asan:
        cdir = Path(cl).parent
        for n in ASAN_RUNTIME:
            if (cdir / n).exists():
                shutil.copy2(cdir / n, bdir / n)
        if not (bdir / ASAN_RUNTIME[0]).exists():
            fail(f'{ASAN_RUNTIME[0]} not found beside {cl}')
    print(f'test_pages_build: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
