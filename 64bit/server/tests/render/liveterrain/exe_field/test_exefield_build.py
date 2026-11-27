"""Build test_exefield.exe, the offline tests of the EXE field write (epic 0465, ticket 4905).
Model: server/tests/render/liveterrain/test_liveterrain_build.py (4907).

    python -I perf/quietlock/quietlock.py run --who w6-4905 --what "4905 exe field tests build" -- \
        python -I server/tests/render/liveterrain/exe_field/test_exefield_build.py --out DIR [--asan]

test_exefield.exe and exefield_live.exe (TEST ONLY, the live check) = server/src/render/srv_liveterrain_exefield.cpp, render/src/heightfield/heightfield.cpp (the mock's
memory image), render/src/common/status.cpp and test_exefield.cpp / exefield_live.cpp. No D3D, no install, no EXE.
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD (the server's toolchain, tools/x64-dll/build.py). --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime copied beside it (issue 0149).
Exit 0 built, 1 a step failed (last line `test_exefield_build: FAILED: <why>`), 3 the launch gate refused.
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
SRC = [REPO / 'server' / 'src' / 'render' / 'srv_liveterrain_exefield.cpp', REPO / 'render' / 'src' / 'heightfield' / 'heightfield.cpp',
       REPO / 'render' / 'src' / 'common' / 'status.cpp', HERE / 'test_exefield.cpp']
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'render' / 'include']
DEFINES = []   # ticket 4966: /DVP_REQUIRE_EXE_TABLES once the header is emitted
LIBS = ['kernel32.lib', 'windowscodecs.lib', 'ole32.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'test_exefield_build: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('exe field tests build (server/tests/render/liveterrain/exe_field/test_exefield_build.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    x64build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(x64build)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # ticket 4966: the EXE build table (vp_exe_tables.gen.h from the local tables, setting exe_tables)
    cp = subprocess.run([sys.executable, '-I', str(REPO / 'tools' / 'exebuild' / 'emit_header.py'), '--out',
                         str(out / 'gen-exebuild')], capture_output=True, text=True, errors='replace')
    print((cp.stdout + cp.stderr).rstrip(), flush=True)
    if cp.returncode != 0:
        fail('the EXE address header (tools/exebuild/emit_header.py)')
    INCLUDES.extend([REPO / 'exebuild' / 'include', out / 'gen-exebuild'])
    DEFINES.append('/DVP_REQUIRE_EXE_TABLES')   # the product's form of srv_liveterrain_exefield.cpp
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
    # test_exefield.exe (the offline tests) and exefield_live.exe (TEST ONLY: the live check on a test instance)
    for name, main_src in (('test_exefield', HERE / 'test_exefield.cpp'), ('exefield_live', HERE / 'exefield_live.cpp')):
        exe = bdir / f'{name}.exe'
        sub = odir / name
        sub.mkdir(parents=True, exist_ok=True)
        cmd = cc + DEFINES + [f'/I{p}' for p in INCLUDES] + [f'/Fe{exe}', f'/Fo{sub}\\', f'/Fd{sub / "vc.pdb"}'] + \
            [str(s) for s in SRC[:-1] + [main_src]] + ['/link', '/DEBUG'] + LIBS
        print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
        cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                            env=env, cwd=str(sub))
        if (cp.stdout + cp.stderr).strip():
            print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
        if cp.returncode != 0 or not exe.exists():
            fail(f'{name}.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'test_exefield_build: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
