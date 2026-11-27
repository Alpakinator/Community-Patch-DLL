"""Build trigger_scene.exe, ticket 4722's tool that plays one recorded fight on the install's art through the server's
fight clock and the trigger emitter (server/tests/effects/trigger_scene.cpp; epic 0042, 2026-11-20).

    set C5_JOB_OWNER=0042
    python -I server/tests/effects/build_trigger_scene.py --out DIR [--asan] [--c5render DIR]

trigger_scene.exe = server/src/effects/srv_trigger_emitter.cpp, srv_trigger_logsink.cpp, server/src/units/
srv_units_fights.cpp and server/tests/effects/trigger_scene.cpp, linked with c5render.lib and c5r_models.lib (--c5render
DIR holding both as render/build.py lays them out: DIR/c5render.lib, DIR/src/units/c5r_models.lib; else render/build.py
--target c5render into DIR/c5render[-asan]). Normal: clang-cl x64 /std:c++14 /W4 /WX /O2 /MD. --asan: MSVC cl
/fsanitize=address into DIR/bin-asan with the ASan runtime beside the binary. Pattern of
server/tests/units/build_units_tests.py. Exit 0 built, 1 a step failed (last line `build_trigger_scene: FAILED: <why>`).
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
NOWIN = 0x08000000
INCLUDES = [REPO / 'server' / 'src', REPO / 'server' / 'src' / 'render', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include', HERE]
SOURCES = [REPO / 'server' / 'src' / 'effects' / 'srv_trigger_emitter.cpp',
           REPO / 'server' / 'src' / 'effects' / 'srv_trigger_logsink.cpp',
           REPO / 'server' / 'src' / 'effects' / 'srv_trigger_tap.cpp',   # compiled and linked as a check of the hand-off's tap
           REPO / 'server' / 'src' / 'effects' / 'srv_effect_routes.cpp',   # 4793: trigger_scene --effects
           REPO / 'server' / 'src' / 'effects' / 'srv_fight_geometry.cpp',   # 4728: trigger_scene --geometry
           REPO / 'server' / 'src' / 'units' / 'srv_units_fights.cpp',
           HERE / 'trigger_scene.cpp']
LIBS = ['ole32.lib', 'user32.lib', 'advapi32.lib', 'kernel32.lib', 'shell32.lib', 'bcrypt.lib', 'windowscodecs.lib',
        'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'psapi.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']


def fail(msg):
    print(f'build_trigger_scene: FAILED: {msg}', flush=True)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--c5render', type=Path)
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('trigger scene build (server/tests/effects/build_trigger_scene.py)')
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
        cc = [cl, '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/Zi', '/Oy-', '/O2', '/fsanitize=address',
              '/D_CRT_SECURE_NO_WARNINGS']
        link = [cl, '/nologo', '/MD', '/fsanitize=address', '/Zi']
    else:
        cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
              '/D_CRT_SECURE_NO_WARNINGS']
        link = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/Zi', '-fuse-ld=lld']
    inc = [f'/I{p}' for p in INCLUDES]
    objs, ok = [], True
    for src in SOURCES:
        obj = odir / (src.stem + '.obj')
        cmd = cc + inc + ['/c', f'/Fo{obj}', f'/Fd{odir / (src.stem + ".pdb")}', str(src)]
        cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                            env=env, cwd=str(odir))
        text = '\n'.join(l for l in (cp.stdout + cp.stderr).splitlines() if l.strip() and l.strip() != src.name)
        if cp.returncode != 0 or text:
            print(f'compile {src.name}: rc={cp.returncode}\n' + text[-6000:], flush=True)
        ok &= cp.returncode == 0
        objs.append(obj)
    if not ok:
        fail('compile errors')
    exe = bdir / 'trigger_scene.exe'
    cmd = link + [f'/Fe{exe}'] + [str(o) for o in objs] + ['/link', '/DEBUG'] + [str(l) for l in libs] + LIBS
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN, env=env,
                        cwd=str(odir))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    if cp.returncode != 0 or not exe.exists():
        fail('link trigger_scene.exe')
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing')
    print(f'build_trigger_scene: OK: {exe}', flush=True)


if __name__ == '__main__':
    main()
