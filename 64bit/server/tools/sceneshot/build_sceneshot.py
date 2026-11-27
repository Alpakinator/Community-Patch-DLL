"""Build sceneshot.exe, the offline scene harness's player (epic 0301, task 3905). Written 2026-11-15 for this repository
(GPL-3.0-or-later).

    python -I perf/quietlock/quietlock.py run --who 3905 --what "sceneshot build" -- \
        python -I server/tools/sceneshot/build_sceneshot.py --out DIR [--asan] [--c5render DIR] [--common-objs DIR] [--smoke]

The player is linked from the same objects as render_host_test.exe and the scenebundle tests: the source list, include
folders, libraries and compiler flags are read from server/tests/render/scenebundle/build_scenebundle_tests.py (imported,
not copied), so a unit added there is in the player too.
1. c5render (render/build.py --target c5render, its own gate) into DIR/c5render[-asan], unless --c5render names a folder
   holding a c5render.lib of the same variant.
2. The common objects (every server/src/render/*.cpp, ring_sink, srv_feed, vp64_options, and since issue 0440 the unit
   scene server/src/units/*.cpp with c5r_models.lib) into DIR/obj[-asan]/common,
   unless --common-objs names a folder that already holds them (a scenebundle build's obj[-asan]/common, same variant).
3. DIR/bin[-asan]/sceneshot.exe = server/tools/sceneshot/sceneshot.cpp + the common objects + c5render + the libraries.
   --asan: MSVC cl /fsanitize=address, the ASan runtime copied beside the binary (issue 0149).
--smoke: runs the built player twice and asserts the exit codes and last lines of two negatives that need no GPU input:
   no selector (2, Usage) and a missing bundle (21, NoManifest, after "scenebundle: REFUSED NoManifest").
Exit 0 built (and the smoke passed), 1 a step failed (last line `build_sceneshot: FAILED: <why>`), 3 the launch gate refused.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
NOWIN = 0x08000000  # CREATE_NO_WINDOW


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def fail(msg):
    print(f'build_sceneshot: FAILED: {msg}', flush=True)
    sys.exit(1)


def smoke(exe):
    """Two negatives of the built binary: exit code and last line each."""
    cases = [([], 2, 'sceneshot: FAILED 2 Usage:'),
             (['--bundle', str(Path(tempfile.gettempdir()) / 'sceneshot_no_such_bundle_3905'), '--out',
               str(Path(tempfile.gettempdir()) / 'sceneshot_smoke_out_3905'), '--dumped'], 21, 'sceneshot: FAILED 21 NoManifest:')]
    for args, want, last in cases:
        cp = subprocess.run([str(exe)] + args, capture_output=True, text=True, errors='replace', creationflags=NOWIN, timeout=120)
        lines = [ln for ln in (cp.stdout + cp.stderr).splitlines() if ln.strip()]
        got = lines[-1] if lines else ''
        if cp.returncode != want or not got.startswith(last):
            fail(f'smoke {args}: exit {cp.returncode} (want {want}), last line {got!r} (want {last!r}...)')
        if want == 21 and not any(ln.startswith('scenebundle: REFUSED NoManifest') for ln in lines):
            fail('smoke: no "scenebundle: REFUSED NoManifest" line')
        if (Path(tempfile.gettempdir()) / 'sceneshot_smoke_out_3905').exists():
            fail('smoke: the refused run created --out')
        print(f'build_sceneshot: smoke OK: exit {cp.returncode}, {got}', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--c5render', type=Path, help='a folder holding c5render.lib of the same variant (else built)')
    ap.add_argument('--common-objs', type=Path, help='a folder holding the common objects of the same variant (else compiled)')
    ap.add_argument('--smoke', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('sceneshot build (server/tools/sceneshot/build_sceneshot.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    sbt = load_module('build_scenebundle_tests', REPO / 'server' / 'tests' / 'render' / 'scenebundle' / 'build_scenebundle_tests.py')
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rdir = a.c5render.resolve() if a.c5render else out / ('c5render-asan' if a.asan else 'c5render')
    if not a.c5render:
        cmd = [sys.executable, '-I', str(REPO / 'render' / 'build.py'), '--build-dir', str(rdir), '--target', 'c5render']
        if a.asan:
            cmd.append('--asan')
        if sbt.run(cmd, cwd=str(REPO)) != 0:
            fail('render/build.py --target c5render')
    lib = rdir / 'c5render.lib'
    if not lib.exists():
        fail(f'{lib} missing')
    mlib = sbt.models_lib(rdir)   # issue 0440: the unit scene (server/src/units/, a common unit) links it
    if not mlib.exists():
        fail(f'{mlib} missing (render/build.py --target c5render builds it)')
    x64build = load_module('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m), out / 'vcvars-env.json')
    env = dict(env, VSCMD_SKIP_SENDTELEMETRY='1')
    sfx = '-asan' if a.asan else ''
    bdir, odir = out / ('bin' + sfx), out / ('obj' + sfx)
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
    inc = [f'/I{p}' for p in sbt.INCLUDES if p != sbt.HERE] + [f'/I{HERE}']
    cdir = a.common_objs.resolve() if a.common_objs else odir / 'common'
    objs = [cdir / (s.stem + '.obj') for s in sbt.COMMON_SRC]
    if not a.common_objs:
        cdir.mkdir(parents=True, exist_ok=True)
        par = ['/MP'] if a.asan else []
        if sbt.run(cc + par + inc + ['/c', f'/Fo{cdir}\\', f'/Fd{cdir / "vc.pdb"}', '/FS'] + [str(s) for s in sbt.COMMON_SRC],
                   env=env, cwd=str(cdir)) != 0:
            fail('the common objects (server/src/render/*.cpp and the units beside them)')
    missing = [o.name for o in objs if not o.exists()]
    if missing:
        fail('common objects missing in ' + str(cdir) + ': ' + ', '.join(missing))
    # ticket 0495: the forest scene (server/src/forest/*.cpp, as server.exe links it) in the player only: a bundle with
    # render_forest=1 replays the trees. The scenebundle test programs do not link it (their host keeps render_forest 0).
    fdir = odir / 'forest'
    fdir.mkdir(parents=True, exist_ok=True)
    fsrc = sorted((REPO / 'server' / 'src' / 'forest').glob('*.cpp'))
    if sbt.run(cc + inc + ['/c', f'/Fo{fdir}\\', f'/Fd{fdir / "vc.pdb"}', '/FS'] + [str(s) for s in fsrc], env=env, cwd=str(fdir)) != 0:
        fail('the forest scene (server/src/forest/*.cpp)')
    objs = objs + [fdir / (s.stem + '.obj') for s in fsrc]
    # ticket 4938: the extras module (server/src/extras/*.cpp, as server.exe links it) in the player only, so that
    # --option draws an extra; the scene tests do not link it (their scenes get no module: MakeSceneExtras null)
    edir = odir / 'extras'
    edir.mkdir(parents=True, exist_ok=True)
    esrc = sorted((REPO / 'server' / 'src' / 'extras').glob('*.cpp'))
    if sbt.run(cc + inc + ['/c', f'/Fo{edir}\\', f'/Fd{edir / "vc.pdb"}', '/FS'] + [str(s) for s in esrc], env=env, cwd=str(edir)) != 0:
        fail('the extras module (server/src/extras/*.cpp)')
    objs = objs + [edir / (s.stem + '.obj') for s in esrc]
    # epic 0455 (4981, 4983): the unit extras module (server/src/extras/units/*.cpp, as server.exe links it) in the player
    # only, so that --option draws a unit extra; the scene tests do not link it (their unit scenes get no module)
    udir = odir / 'extras_units'
    udir.mkdir(parents=True, exist_ok=True)
    usrc = sorted((REPO / 'server' / 'src' / 'extras' / 'units').glob('*.cpp'))
    if sbt.run(cc + inc + ['/c', f'/Fo{udir}\\', f'/Fd{udir / "vc.pdb"}', '/FS'] + [str(s) for s in usrc], env=env, cwd=str(udir)) != 0:
        fail('the unit extras module (server/src/extras/units/*.cpp)')
    objs = objs + [udir / (s.stem + '.obj') for s in usrc]
    tdir = odir / 'sceneshot'
    tdir.mkdir(parents=True, exist_ok=True)
    exe = bdir / 'sceneshot.exe'
    rc = sbt.run(cc + inc + [f'/Fe{exe}', f'/Fo{tdir}\\', f'/Fd{tdir / "vc.pdb"}', str(HERE / 'sceneshot.cpp')] + [str(o) for o in objs] +
                 ['/link', '/DEBUG', str(lib), str(mlib)] + sbt.LIBS, env=env, cwd=str(tdir))
    if rc != 0 or not exe.exists():
        fail('sceneshot.exe')
    if a.asan:
        for name in sbt.ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing (the ASan runtime of the cl.exe that built the binary)')
    if a.smoke:
        smoke(exe)
    print(f'build_sceneshot: OK: {exe}', flush=True)


if __name__ == '__main__':
    main()
