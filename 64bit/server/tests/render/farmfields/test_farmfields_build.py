"""Build test_farmfields_producer.exe, the tests of the farm producer in server.exe's renderer (epic 0528, ticket 4852).
Model: server/tests/render/forest/test_forest_build.py.

    python -I perf/quietlock/quietlock.py run --who 0528-4852 --what "4852 farm producer tests build" -- \
        python -I server/tests/render/farmfields/test_farmfields_build.py --out DIR [--asan] [--c5render DIR]

test_farmfields_producer.exe = every server/src/render/*.cpp (the render units server.exe compiles),
shell/frame/ring_sink.cpp, server/src/feed/srv_feed.cpp, options/src/vp64_options.cpp +
server/tests/render/farmfields/test_farmfields_producer.cpp, linked with c5render and c5r_models (--c5render DIR holding
c5render.lib and src/units/c5r_models.lib, else render/build.py --target c5render into DIR/c5render[-asan], which builds
both) and the D3D libraries. The include path carries models/include as server/build_server.py's does: the render units'
c5r/pathmodels/pathmodels.h (ticket 0300) includes c5models/anim.h, which server/tests/render/build_render_tests.py's
list lacks (found 2026-11-18 by 4852; that builder is not changed here).
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime copied beside it (issue 0149).
Exit 0 built, 1 a step failed (last line `test_farmfields_build: FAILED: <why>`), 3 the launch gate refused.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
NOWIN = 0x08000000
SRC = sorted((REPO / 'server' / 'src' / 'render').glob('*.cpp')) + [
    REPO / 'shell' / 'frame' / 'ring_sink.cpp', REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp',
    REPO / 'options' / 'src' / 'vp64_options.cpp', HERE / 'test_farmfields_producer.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include']
LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'psapi.lib', 'user32.lib',
        'advapi32.lib', 'kernel32.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'test_farmfields_build: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--c5render', type=Path, help='a folder holding c5render.lib of the same variant (else built)')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('farm producer tests build (server/tests/render/farmfields/test_farmfields_build.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    x64build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(x64build)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rdir = a.c5render.resolve() if a.c5render else out / ('c5render-asan' if a.asan else 'c5render')
    if not a.c5render:
        cmd = [sys.executable, '-I', str(REPO / 'render' / 'build.py'), '--build-dir', str(rdir), '--target', 'c5render']
        if a.asan:
            cmd.append('--asan')
        if subprocess.run(cmd, cwd=str(REPO), creationflags=NOWIN).returncode != 0:
            fail('render/build.py --target c5render')
    lib = rdir / 'c5render.lib'
    models = rdir / 'src' / 'units' / 'c5r_models.lib'   # render/src/units/CMakeLists.txt builds it with c5render
    for need in (lib, models):
        if not need.exists():
            fail(f'{need} missing')
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
    exe = bdir / 'test_farmfields_producer.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG', str(lib), str(models)] + LIBS
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('test_farmfields_producer.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'test_farmfields_build: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
