"""Build the test programs of the renderer inside server.exe (issue 3301, epic 3300, 2026-11-13).

    python -I perf/quietlock/quietlock.py run --who 3300-w1 --what "render host tests build" -- \
        python -I server/tests/render/build_render_tests.py --out DIR [--asan]

1. c5render (render/build.py --target c5render, gated) into DIR/c5render (DIR/c5render-asan with --asan).
2. render_host_test.exe: the render units of server.exe (server/src/render/*.cpp, shell/frame/ring_sink.cpp,
   server/src/feed/srv_feed.cpp; the same sources build_server.py compiles into server.exe, without the DLL fork's
   include path) + server/tests/render/render_host_test.cpp, linked with c5render and the D3D libraries.
3. fake_consumer.exe: the consumer half of the fake composite (server/tests/render/fake_consumer.cpp).
Normal: clang-cl x64 (the server's toolchain, tools/x64-dll/build.py) /std:c++14 /W3 /WX /O2 /MD.
--asan: MSVC cl /fsanitize=address /Zi /Oy- /W3 /WX (the clang-cl ASan runtime of LLVM 19 crashes at start-up here,
render/README.md), c5render from render/build.py --asan; outputs DIR/bin-asan, with the ASan runtime
(clang_rt.asan_dynamic-x86_64.dll from cl.exe's folder) copied beside the binaries (issue 0149: no PATH change needed).
Exit 0 built, 1 a step failed (last line `build_render_tests: FAILED: <why>`), 3 the launch gate refused.
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
NOWIN = 0x08000000  # CREATE_NO_WINDOW
RENDER_SRC = sorted((REPO / 'server' / 'src' / 'render').glob('*.cpp'))
HOST_SRC = RENDER_SRC + [REPO / 'shell' / 'frame' / 'ring_sink.cpp', REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp',
                         HERE / 'render_host_test.cpp', HERE / 'bakefake.cpp',
                         REPO / 'options' / 'src' / 'vp64_options.cpp']   # issue 3308: SrvOptions() of the test host
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include']
# 2026-11-18 (MC, after ticket 0300): srv_render_scene.h includes c5r/pathmodels/pathmodels.h, which includes
# c5models/anim.h, and pathmodels' code in c5render uses c5r_models (as test_fow_build.py since 0424)
D3D_LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'psapi.lib',
            'user32.lib', 'advapi32.lib', 'kernel32.lib']
# issue 0149: the runtime of cl /fsanitize=address /MD (beside cl.exe in bin/HostX64/x64); the .pdb only symbolizes it
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'build_render_tests: FAILED: {msg}', flush=True)
    sys.exit(1)


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    return cp.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--skip-c5render', action='store_true', help='reuse DIR/c5render[-asan]/c5render.lib')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('render host tests build (server/tests/render/build_render_tests.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rdir = out / ('c5render-asan' if a.asan else 'c5render')
    if not a.skip_c5render:
        cmd = [sys.executable, '-I', str(REPO / 'render' / 'build.py'), '--build-dir', str(rdir), '--target', 'c5render']
        if a.asan:
            cmd.append('--asan')
        if run(cmd, cwd=str(REPO)) != 0:
            fail('render/build.py --target c5render')
    lib = rdir / 'c5render.lib'
    if not lib.exists():
        fail(f'{lib} missing')
    mlib = rdir / 'src' / 'units' / 'c5r_models.lib'   # render/build.py lays it out beside c5render.lib
    if not mlib.exists():
        fail(f'{mlib} missing')
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m), out / 'vcvars-env.json')
    env = dict(env, VSCMD_SKIP_SENDTELEMETRY='1')
    bdir = out / ('bin-asan' if a.asan else 'bin')
    odir = out / ('obj-asan' if a.asan else 'obj')
    bdir.mkdir(parents=True, exist_ok=True)
    odir.mkdir(parents=True, exist_ok=True)
    inc = [f'/I{p}' for p in INCLUDES]
    if a.asan:
        # CreateProcess resolves the program with this process's PATH, not env's: the full path of the vcvars cl.exe
        cl = shutil.which('cl.exe', path=env.get('PATH') or env.get('Path'))
        if not cl:
            fail('cl.exe not on the vcvarsall PATH')
        cc = [cl, '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/Zi', '/Oy-', '/O2', '/fsanitize=address',
              '/D_CRT_SECURE_NO_WARNINGS']   # container annotations stay on: c5render's ASan build has them (LNK2038)
    else:
        cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
              '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS']
    host = bdir / 'render_host_test.exe'
    rc = run(cc + inc + [f'/Fe{host}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in HOST_SRC] +
             ['/link', '/DEBUG', str(lib), str(mlib)] + D3D_LIBS, env=env, cwd=str(odir))
    if rc != 0 or not host.exists():
        fail('render_host_test.exe')
    fake = bdir / 'fake_consumer.exe'
    rc = run(cc + [f'/I{REPO / "shell" / "common"}', f'/Fe{fake}', f'/Fo{odir}\\', f'/Fd{odir / "vc2.pdb"}',
                   str(HERE / 'fake_consumer.cpp'), '/link', '/DEBUG', 'd3d11.lib', 'dxgi.lib', 'kernel32.lib'],
             env=env, cwd=str(odir))
    if rc != 0 or not fake.exists():
        fail('fake_consumer.exe')
    if a.asan:
        # issue 0149: the MSVC ASan runtime beside the binaries, so the suite needs no PATH change (without it every test
        # that starts render_host_test.exe fails with 0xC0000135, STATUS_DLL_NOT_FOUND)
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if not src.exists():
                if name.endswith('.dll'):
                    fail(f'{src} missing (the ASan runtime of the cl.exe that built the binaries)')
                continue
            shutil.copy2(src, bdir / name)
            print(f'build_render_tests: copied {src} -> {bdir}', flush=True)
    print(f'build_render_tests: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
