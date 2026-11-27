"""Build the test program of the server's side of the model tap (ticket 4875, epic 0561), by the pattern of
server/tests/render/decal/build_decal_tests.py.

    python -I perf/quietlock/quietlock.py run --who 0561-4875 --what "4875 models tests build" -- \
        python -I server/tests/render/cities/build_models_tests.py --out DIR [--asan]

models_test.exe = server/src/render/landmarks/srv_model_tap.cpp, srv_models_scene.cpp, server/src/render/cities/
srv_models_mode.cpp, srv_render_common.cpp, server/src/feed/srv_feed.cpp (the sources server.exe compiles) +
render/src/common/status.cpp + server/tests/render/cities/models_test.cpp. No c5render build and no D3D device: the
module runs with the test's stand-in layer (the c5r adapter, srv_model_layer_c5r.cpp, is not linked here).
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl
/fsanitize=address (as build_render_tests.py) into DIR/bin-asan with the ASan runtime copied beside it.
Outputs DIR/bin[-asan]/models_test.exe. Exit 0 built, 1 a step failed (last line `build_models_tests: FAILED: <why>`),
3 the launch gate refused.
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
RSRC = REPO / 'server' / 'src' / 'render'
SRC = [RSRC / 'landmarks' / 'srv_model_tap.cpp', RSRC / 'landmarks' / 'srv_models_scene.cpp',
       RSRC / 'landmarks' / 'srv_model_geom.cpp',   # ticket 4879
       RSRC / 'cities' / 'srv_models_mode.cpp', RSRC / 'srv_render_common.cpp',
       REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp', REPO / 'render' / 'src' / 'common' / 'status.cpp',
       HERE / 'models_test.cpp']
INCLUDES = [RSRC, REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'render' / 'include']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'build_models_tests: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('models test build (server/tests/render/cities/build_models_tests.py)')
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
    exe = bdir / 'models_test.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG', 'kernel32.lib', 'user32.lib']
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('models_test.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'build_models_tests: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
