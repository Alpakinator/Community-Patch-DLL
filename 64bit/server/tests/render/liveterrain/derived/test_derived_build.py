"""Build test_derived.exe, the offline proof of ticket 4904 (epic 0465): the trees on the new ground after a live window.
Model: server/tests/render/forest/test_forest_build.py.

    python -I perf/quietlock/quietlock.py run --who 0465-4904 --what "4904 derived tests build" -- \
        python -I server/tests/render/liveterrain/derived/test_derived_build.py --out DIR [--c5render DIR]

test_derived.exe = server/src/render/srv_liveterrain_derived.cpp + test_derived.cpp, linked with c5render.lib
(--c5render DIR holding it, else render/build.py --target c5render into DIR/c5render). clang-cl x64 /std:c++14 /W3 /WX
/O2 /MD (the server's toolchain, tools/x64-dll/build.py).
Exit 0 built, 1 a step failed (last line `test_derived_build: FAILED: <why>`), 3 the launch gate refused.
"""
import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]
NOWIN = 0x08000000
SRC = [REPO / 'server' / 'src' / 'render' / 'srv_liveterrain_derived.cpp', HERE / 'test_derived.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include']
LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'user32.lib', 'advapi32.lib',
        'kernel32.lib']


def fail(msg):
    print(f'test_derived_build: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--c5render', type=Path, help='a folder holding c5render.lib (else built)')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('4904 derived tests build (server/tests/render/liveterrain/derived/test_derived_build.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    x64build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(x64build)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rdir = a.c5render.resolve() if a.c5render else out / 'c5render'
    if not a.c5render:
        cmd = [sys.executable, '-I', str(REPO / 'render' / 'build.py'), '--build-dir', str(rdir), '--target', 'c5render']
        if subprocess.run(cmd, cwd=str(REPO), creationflags=NOWIN).returncode != 0:
            fail('render/build.py --target c5render')
    lib = rdir / 'c5render.lib'
    if not lib.exists():
        fail(f'{lib} missing')
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m), out / 'vcvars-env.json')
    env = dict(env, VSCMD_SKIP_SENDTELEMETRY='1')
    bdir = out / 'bin'
    odir = out / 'obj'
    bdir.mkdir(parents=True, exist_ok=True)
    odir.mkdir(parents=True, exist_ok=True)
    cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi', '-fuse-ld=lld',
          '/D_CRT_SECURE_NO_WARNINGS']
    exe = bdir / 'test_derived.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG', str(lib)] + LIBS
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('test_derived.exe')
    print(f'test_derived_build: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
