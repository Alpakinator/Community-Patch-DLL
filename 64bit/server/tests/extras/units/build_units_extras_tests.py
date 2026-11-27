"""Build test_units_extras.exe, the tests of the units seam (epic 0455, issue 4981; 2026-11-20).

    set C5_JOB_OWNER=<you>
    python -I perf/quietlock/quietlock.py run --who <you> --what "4981 seam tests build" -- \
        python -I server/tests/extras/units/build_units_extras_tests.py --out DIR --c5render RDIR

test_units_extras.exe = the sources test_scene_units.exe compiles (server/tests/units/build_units_tests.py: every
server/src/render/*.cpp, shell/frame/ring_sink.cpp, every server/src/units/*.cpp, server/src/feed/srv_feed.cpp,
options/src/vp64_options.cpp) + every server/src/extras/units/*.cpp + server/tests/extras/units/test_units_extras.cpp,
linked with RDIR/c5render.lib and RDIR/src/units/c5r_models.lib (render/build.py --target c5render) and the D3D libraries.
clang-cl x64 /std:c++14 /W3 /WX /O2 /MD, as build_units_tests.py. Run: DIR/bin/test_units_extras.exe (no install needed).
Exit 0 built, 1 a step failed (last line `build_units_extras_tests: FAILED: <why>`), 3 the launch gate refused.
"""
import argparse
import concurrent.futures
import importlib.util
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
NOWIN = 0x08000000


def fail(msg):
    print(f'build_units_extras_tests: FAILED: {msg}', flush=True)
    sys.exit(1)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--c5render', type=Path, required=True, help='a render build folder (c5render.lib, src/units/c5r_models.lib)')
    ap.add_argument('--test', default='test_units_extras', help='the test source in this folder, without .cpp (4985: test_fix_air_bases)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) // 2))
    ap.add_argument('--changed', action='store_true',
                    help='compile only sources newer than their object (a header change needs a full build)')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('units seam tests build (server/tests/extras/units/build_units_extras_tests.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    units_build = load('build_units_tests', REPO / 'server' / 'tests' / 'units' / 'build_units_tests.py')
    x64build = load('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rdir = a.c5render.resolve()
    libs = [rdir / 'c5render.lib', rdir / 'src' / 'units' / 'c5r_models.lib']
    for lib in libs:
        if not lib.exists():
            fail(f'{lib} missing')
    srcs, doubles = units_build.sources()
    if doubles:
        fail('the units tests need test doubles (' + ', '.join(d.name for d in doubles) + '): not this branch')
    srcs = [s for s in srcs if s.name != 'test_scene_units.cpp']
    srcs += sorted((REPO / 'server' / 'src' / 'extras' / 'units').glob('*.cpp')) + [HERE / (a.test + '.cpp')]
    stems = [s.stem for s in srcs]
    dup = sorted({s for s in stems if stems.count(s) > 1})
    if dup:
        fail('sources with the same stem: ' + ', '.join(dup))
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m), out / 'vcvars-env.json')
    env = dict(env, VSCMD_SKIP_SENDTELEMETRY='1')
    bdir, odir = out / 'bin', out / 'obj'
    bdir.mkdir(parents=True, exist_ok=True)
    odir.mkdir(parents=True, exist_ok=True)
    cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi', '/D_CRT_SECURE_NO_WARNINGS']
    link = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/Zi', '-fuse-ld=lld']
    inc = [f'/I{p}' for p in units_build.INCLUDES]

    def one(src):
        obj = odir / (src.stem + '.obj')
        if a.changed and obj.exists() and obj.stat().st_mtime > src.stat().st_mtime:
            return src, obj, subprocess.CompletedProcess([], 0, '', '')
        cmd = cc + inc + ['/c', f'/Fo{obj}', f'/Fd{odir / (src.stem + ".pdb")}', str(src)]
        cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                            env=env, cwd=str(odir))
        return src, obj, cp

    objs, ok = [], True
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for src, obj, cp in ex.map(one, srcs):
            text = (cp.stdout + cp.stderr).strip()
            lines = [l for l in text.splitlines() if l.strip() and l.strip() != src.name]
            if cp.returncode != 0 or lines:
                print(f'compile {src.name}: rc={cp.returncode}\n' + '\n'.join(lines)[-6000:], flush=True)
            ok &= cp.returncode == 0
            objs.append(obj)
    if not ok:
        fail('compile errors')
    exe = bdir / (a.test + '.exe')
    cmd = link + [f'/Fe{exe}'] + [str(o) for o in objs] + ['/link', '/DEBUG'] + [str(l) for l in libs] + units_build.LIBS
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN, env=env,
                        cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('link test_units_extras.exe')
    print(f'build_units_extras_tests: OK: {exe}', flush=True)


if __name__ == '__main__':
    main()
