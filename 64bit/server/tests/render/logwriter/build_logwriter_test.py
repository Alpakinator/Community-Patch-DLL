"""Build and run the log writer's test program (ticket 5054). Model: server/tests/render/ice/build_ice_tests.py.

    python -I perf/quietlock/quietlock.py run --who <who> --what "5054 log writer test" -- \
        python -I server/tests/render/logwriter/build_logwriter_test.py --out DIR [--asan] [--run]

logwriter_test.exe = server/src/render/srv_render_common.cpp (the source server.exe compiles) +
server/tests/render/logwriter/logwriter_test.cpp. No D3D, no game files.
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime copied beside it.
--run: runs the program in a fresh DIR/run[-asan] folder (its cases: the docstring of logwriter_test.cpp; research
unit-combat-server.md 18.27.1) and prints its PASS / FAIL lines.
Exit 0 built (and passed), 1 a step failed (last line `build_logwriter_test: FAILED: <why>`), 3 the launch gate refused.
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
SRC = [REPO / 'server' / 'src' / 'render' / 'srv_render_common.cpp', HERE / 'logwriter_test.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'shell' / 'common']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'build_logwriter_test: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--run', action='store_true', help='run the cases after the build')
    ap.add_argument('--timeout', type=int, default=300)
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('log writer test build (server/tests/render/logwriter/build_logwriter_test.py)')
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
    exe = bdir / 'logwriter_test.exe'
    cmd = cc + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{odir}\\', f'/Fd{odir / "vc.pdb"}'] + [str(s) for s in SRC] + \
        ['/link', '/DEBUG', 'kernel32.lib', 'user32.lib']
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                        env=env, cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('logwriter_test.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'build_logwriter_test: built {exe}', flush=True)
    if not a.run:
        print(f'build_logwriter_test: OK: {bdir}', flush=True)
        return
    rdir = out / ('run-asan' if a.asan else 'run')
    if rdir.exists():
        shutil.rmtree(rdir)
    rdir.mkdir(parents=True)
    try:
        cp = subprocess.run([str(exe), str(rdir)], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                            timeout=a.timeout, cwd=str(rdir))
    except subprocess.TimeoutExpired:
        fail(f'logwriter_test.exe did not end within {a.timeout} s')
    print((cp.stdout + cp.stderr).rstrip(), flush=True)
    if cp.returncode != 0:
        fail(f'logwriter_test.exe exit {cp.returncode}')
    print(f'build_logwriter_test: OK: {bdir} (cases passed)', flush=True)


if __name__ == '__main__':
    main()
