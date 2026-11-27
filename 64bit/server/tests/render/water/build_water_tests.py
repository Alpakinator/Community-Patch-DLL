"""Build the test programs of the water mesh tap's reader (issue 3603) and of the water scene (issue 3607), epic 3600.

    python -I perf/quietlock/quietlock.py run --who 3600 --what "3603 reader build" -- \
        python -I server/tests/render/water/build_water_tests.py --out DIR [--asan]

water_reader_test.exe = server/src/render/srv_water_tap.cpp + srv_render_common.cpp (the same sources server.exe
compiles) + server/tests/render/water/water_reader_test.cpp.
water_scene_test.exe (issue 3607) = srv_water_scene.cpp + srv_water_tap.cpp + srv_render_common.cpp + water_scene_test.cpp,
linked with c5render (--c5render DIR holding c5render.lib, else render/build.py --target c5render into DIR/c5render) and
d3d11. Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the
server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl /fsanitize=address (as build_render_tests.py) into
DIR/bin-asan with the ASan runtime copied beside it. Outputs DIR/bin[-asan]/water_reader_test.exe.
Exit 0 built, 1 a step failed (last line `build_water_tests: FAILED: <why>`), 3 the launch gate refused.
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
SRC = [REPO / 'server' / 'src' / 'render' / 'srv_water_tap.cpp', REPO / 'server' / 'src' / 'render' / 'srv_render_common.cpp',
       HERE / 'water_reader_test.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'shell' / 'common', REPO / 'render' / 'include']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'build_water_tests: FAILED: {msg}', flush=True)
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
        gate.require('water reader test build (server/tests/render/water/build_water_tests.py)')
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
    exe = bdir / 'water_reader_test.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG', 'kernel32.lib', 'user32.lib']
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('water_reader_test.exe')
    # issue 3607: the water scene's test program, with c5render
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
    exe2 = bdir / 'water_scene_test.exe'
    src2 = [REPO / 'server' / 'src' / 'render' / n for n in ('srv_water_scene.cpp', 'srv_water_tap.cpp', 'srv_render_common.cpp')] + \
        [HERE / 'water_scene_test.cpp']
    obj2 = odir / 'scene'
    obj2.mkdir(parents=True, exist_ok=True)
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe2}', f'/Fo{obj2}\\', f'/Fd{obj2 / "vc2.pdb"}'] + [str(x) for x in src2] + \
        ['/link', '/DEBUG', str(lib), 'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib',
         'kernel32.lib', 'user32.lib']
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe2.exists():
        fail('water_scene_test.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'build_water_tests: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
