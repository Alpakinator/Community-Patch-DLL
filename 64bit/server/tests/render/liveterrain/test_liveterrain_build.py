"""Build test_liveterrain.exe, the tests of the live-terrain region hook's core (epic 0465, ticket 4907).
Model: server/tests/render/farmfields/test_farmfields_build.py.

    python -I perf/quietlock/quietlock.py run --who w5-4907 --what "4907 live terrain tests build" -- \
        python -I server/tests/render/liveterrain/test_liveterrain_build.py --out DIR [--asan] [--hook-run C5RENDER_DIR]

test_liveterrain.exe = server/src/render/srv_liveterrain.cpp, srv_liveterrain_c5r.cpp and srv_liveterrain_exefield.cpp
(the hook's core, its adapter to 4903's producer, 4905's EXE field writer), render/src/common/status.cpp,
render/src/heightfield/heightfield.cpp and test_liveterrain.cpp, with the test stub of the producer (stub/producer_stub.cpp,
stub/ first on the include path). No D3D, no c5render.
--hook-run C5RENDER_DIR: also lt_hook_run.exe (the hook on recorded inputs with the REAL producer: the real header and
C5RENDER_DIR/c5render.lib from render/build.py --target c5render; no stub).
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime copied beside it (issue 0149).
Exit 0 built, 1 a step failed (last line `test_liveterrain_build: FAILED: <why>`), 3 the launch gate refused.
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
SRC = [REPO / 'server' / 'src' / 'render' / 'srv_liveterrain.cpp', REPO / 'server' / 'src' / 'render' / 'srv_liveterrain_c5r.cpp',
       REPO / 'server' / 'src' / 'render' / 'srv_liveterrain_exefield.cpp',
       REPO / 'render' / 'src' / 'common' / 'status.cpp', REPO / 'render' / 'src' / 'heightfield' / 'heightfield.cpp',
       HERE / 'stub' / 'producer_stub.cpp', HERE / 'test_liveterrain.cpp']
INCLUDES = [HERE / 'stub', REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common',
            REPO / 'render' / 'include']
LIBS = ['bcrypt.lib', 'kernel32.lib', 'windowscodecs.lib', 'ole32.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'test_liveterrain_build: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--hook-run', type=Path, help='a render/build.py --target c5render build folder: also lt_hook_run.exe')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('live terrain tests build (server/tests/render/liveterrain/test_liveterrain_build.py)')
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
    exe = bdir / 'test_liveterrain.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG'] + LIBS
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('test_liveterrain.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    # ticket 0586: test_icewaves.exe, the server's side of the EXE's ice and coast waves (icewaves/test_icewaves.cpp), with
    # the hook's core for FieldRelevantChange (the same sources and stub as test_liveterrain.exe, its own main)
    idir = out / ('obj-icewaves-asan' if a.asan else 'obj-icewaves')
    idir.mkdir(parents=True, exist_ok=True)
    iexe = bdir / 'test_icewaves.exe'
    isrc = [s for s in SRC if s.name != 'test_liveterrain.cpp'] + \
        [REPO / 'server' / 'src' / 'render' / 'srv_liveterrain_icewaves.cpp', HERE / 'icewaves' / 'test_icewaves.cpp']
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{iexe}', f'/Fo{idir}\\', f'/Fd{idir / "vc.pdb"}'] + [str(s) for s in isrc] + \
        ['/link', '/DEBUG'] + LIBS
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(idir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not iexe.exists():
        fail('test_icewaves.exe')
    if a.hook_run:
        lib = a.hook_run.resolve() / 'c5render.lib'
        if not lib.exists():
            fail(f'{lib} missing')
        hdir = out / ('obj-hook-asan' if a.asan else 'obj-hook')
        hdir.mkdir(parents=True, exist_ok=True)
        hexe = bdir / 'lt_hook_run.exe'
        hsrc = [REPO / 'server' / 'src' / 'render' / n for n in ('srv_liveterrain.cpp', 'srv_liveterrain_c5r.cpp',
                                                                   'srv_liveterrain_exefield.cpp')] + [HERE / 'lt_hook_run.cpp']
        hinc = [p for p in INCLUDES if p != HERE / 'stub']
        cmd = cc + [f'/I{p}' for p in hinc] + [f'/Fe{hexe}', f'/Fo{hdir}\\', f'/Fd{hdir / "vc.pdb"}'] + [str(s) for s in hsrc] + \
            ['/link', '/DEBUG', str(lib)] + LIBS
        print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
        cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                            env=env, cwd=str(hdir))
        if (cp.stdout + cp.stderr).strip():
            print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
        if cp.returncode != 0 or not hexe.exists():
            fail('lt_hook_run.exe')
    print(f'test_liveterrain_build: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
