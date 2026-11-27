"""Build test_scene_overlay.exe, the stand-in tests of the overlay in server.exe's terrain scene (epic 0167, issue 3704).

    python -I perf/quietlock/quietlock.py run --who 0167 --what "3704 scene tests build" -- \
        python -I server/tests/render/overlay/test_scene_build.py --out DIR [--asan] [--c5render DIR]

test_scene_overlay.exe = every server/src/render/*.cpp (the render units server.exe compiles), shell/frame/ring_sink.cpp,
server/src/feed/srv_feed.cpp, options/src/vp64_options.cpp (as build_render_tests.py's render_host_test) +
server/tests/render/overlay/test_scene_overlay.cpp, linked with c5render (--c5render DIR holding c5render.lib, else
render/build.py --target c5render into DIR/c5render[-asan]) and the D3D libraries.
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime copied beside it (issue 0149).
Exit 0 built, 1 a step failed (last line `test_scene_build: FAILED: <why>`), 3 the launch gate refused.
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
    REPO / 'options' / 'src' / 'vp64_options.cpp', HERE / 'test_scene_overlay.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include']
# ticket 0536 (2026-11-18): models/include and c5r_models.lib since ticket 0300 (srv_render_scene.h includes
# c5r/pathmodels/pathmodels.h, which includes c5models/anim.h; pathmodels' code in c5render uses c5r_models), as
# fow/test_fow_build.py since 0424 and build_render_tests.py since c003aee5d
LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'psapi.lib', 'user32.lib',
        'advapi32.lib', 'kernel32.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'test_scene_build: FAILED: {msg}', flush=True)
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
        gate.require('overlay scene tests build (server/tests/render/overlay/test_scene_build.py)')
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
    if not lib.exists():
        fail(f'{lib} missing')
    mlib = rdir / 'src' / 'units' / 'c5r_models.lib'   # ticket 0536: render/build.py lays it out beside c5render.lib
    if not mlib.exists():
        fail(f'{mlib} missing')
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
    exe = bdir / 'test_scene_overlay.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG', str(lib), str(mlib)] + LIBS
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('test_scene_overlay.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'test_scene_build: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
