"""Build test_scene_units.exe, the tests of the server's unit scene (epic 0375, issue 4454; 2026-11-15).

    set C5_JOB_OWNER=0375
    python -I perf/quietlock/quietlock.py run --who 0375 --what "4454 unit scene tests build" -- \
        python -I server/tests/units/build_units_tests.py --out DIR [--asan] [--c5render DIR]

test_scene_units.exe = every server/src/render/*.cpp and server/src/units/*.cpp (the render units server.exe compiles,
build_server.render_sources), server/src/feed/srv_feed.cpp, options/src/vp64_options.cpp + server/tests/units/
test_scene_units.cpp, linked with c5render.lib and c5r_models.lib (--c5render DIR holding both as render/build.py lays them
out: DIR/c5render.lib, DIR/src/units/c5r_models.lib; else render/build.py --target c5render into DIR/c5render[-asan]) and
the D3D libraries. Test doubles (server/tests/units/, never server/src/): seed_double.cpp while
server/src/units/srv_units_seed.cpp does not exist (issue 4453), feed_units_double.cpp while srv_feed.cpp defines no
CopyUnits (issue 4452); the build prints which ones it took.
Normal: clang-cl x64 /std:c++14 /W3 /WX /O2 /MD. --asan: MSVC cl /fsanitize=address into DIR/bin-asan with the ASan
runtime copied beside the binary (issue 0149).
Exit 0 built, 1 a step failed (last line `build_units_tests: FAILED: <why>`), 3 the launch gate refused.
"""
import argparse
import concurrent.futures
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
NOWIN = 0x08000000
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include']
LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'psapi.lib', 'user32.lib',
        'advapi32.lib', 'kernel32.lib', 'shell32.lib', 'bcrypt.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'build_units_tests: FAILED: {msg}', flush=True)
    sys.exit(1)


def sources():
    src = sorted((REPO / 'server' / 'src' / 'render').glob('*.cpp')) + [REPO / 'shell' / 'frame' / 'ring_sink.cpp']
    src += sorted((REPO / 'server' / 'src' / 'units').glob('*.cpp'))
    src += sorted((REPO / 'server' / 'src' / 'effects').glob('*.cpp'))   # ticket 4722: srv_units_anim.cpp calls the trigger tap
    src += [REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp', REPO / 'options' / 'src' / 'vp64_options.cpp']
    doubles = []
    if not (REPO / 'server' / 'src' / 'units' / 'srv_units_seed.cpp').exists():
        doubles.append(HERE / 'seed_double.cpp')
    if 'CopyUnits(' not in (REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp').read_text(encoding='utf-8'):
        doubles.append(HERE / 'feed_units_double.cpp')
    return src + doubles + [HERE / 'test_scene_units.cpp'], doubles


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--c5render', type=Path, help='a render build folder of the same variant (c5render.lib, src/units/c5r_models.lib)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) // 2))
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('unit scene tests build (server/tests/units/build_units_tests.py)')
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
    libs = [rdir / 'c5render.lib', rdir / 'src' / 'units' / 'c5r_models.lib']
    for lib in libs:
        if not lib.exists():
            fail(f'{lib} missing')
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
        link = [cl, '/nologo', '/MD', '/fsanitize=address', '/Zi']
    else:
        cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
              '/D_CRT_SECURE_NO_WARNINGS']
        link = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/Zi', '-fuse-ld=lld']
    srcs, doubles = sources()
    print('build_units_tests: test doubles: ' + (', '.join(d.name for d in doubles) or 'none (4452 and 4453 merged)'), flush=True)
    stems = [s.stem for s in srcs]
    dup = sorted({s for s in stems if stems.count(s) > 1})
    if dup:
        fail('sources with the same stem: ' + ', '.join(dup))
    inc = [f'/I{p}' for p in INCLUDES]

    def one(src):
        obj = odir / (src.stem + '.obj')
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
    exe = bdir / 'test_scene_units.exe'
    cmd = link + [f'/Fe{exe}'] + [str(o) for o in objs] + ['/link', '/DEBUG'] + [str(l) for l in libs] + LIBS
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN, env=env,
                        cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('link test_scene_units.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'build_units_tests: OK: {exe}', flush=True)


if __name__ == '__main__':
    main()
