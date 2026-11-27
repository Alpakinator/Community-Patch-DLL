"""Offline unit test of the server's VFS resolution rule (bug 0100). About 5 s; no game, no DLL, no fork source.

  python server/tests/run_vfs_test.py [--vfs-src FILE] [--out DIR]

Makes a fake game folder (below), copies server/src/srv_vfs.cpp (or --vfs-src, e.g. an older revision written out with
`git show <rev>:server/src/srv_vfs.cpp`) into a build folder with server/tests/vfs_test_common.h beside it as
srv_common.h, compiles it with server/tests/test_vfs_resolve.cpp (clang-cl x64, the toolchain of tools/x64-dll/build.py)
and runs the test on the fake folder. Exit 0 when every case passes, 1 when a case fails, 2 when the build fails.

The fake game folder (each file holds the word after the arrow):
  Assets/Maps/Communitu_79a.lua                                  -> base     (the base game's old copy)
  Assets/Maps/Continents.lua                                     -> basecont
  Assets/DLC/Low/Low.Civ5Pkg                                       <Priority>5</Priority>
  Assets/DLC/Low/Maps/Communitu_79a.lua                          -> low
  Assets/DLC/Low/Mods/Shared.lua                                 -> low-mods
  Assets/DLC/Low/UI/Shared.lua                                   -> low-ui   (later in the walk: wins in its package)
  Assets/DLC/VP/VP.Civ5Pkg                                         <Priority>10</Priority>
  Assets/DLC/VP/Mods/(2) Vox Populi/Mapscripts/Communitu_79a.lua -> vp       (highest priority: wins by name)
  Assets/DLC/Expansion/Maps/OnlyInExpansion.lua                  -> exp      (package left out of the index)
  Extra/Outside.lua                                              -> outside  (outside Assets)
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

FILES = {
    'Assets/Maps/Communitu_79a.lua': 'base',
    'Assets/Maps/Continents.lua': 'basecont',
    'Assets/DLC/Low/Low.Civ5Pkg': '<Civ5Package><Priority>5</Priority></Civ5Package>',
    'Assets/DLC/Low/Maps/Communitu_79a.lua': 'low',
    'Assets/DLC/Low/Mods/Shared.lua': 'low-mods',
    'Assets/DLC/Low/UI/Shared.lua': 'low-ui',
    'Assets/DLC/VP/VP.Civ5Pkg': '<Civ5Package><Priority>10</Priority></Civ5Package>',
    'Assets/DLC/VP/Mods/(2) Vox Populi/Mapscripts/Communitu_79a.lua': 'vp',
    'Assets/DLC/Expansion/Maps/OnlyInExpansion.lua': 'exp',
    'Extra/Outside.lua': 'outside',
}


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--vfs-src', type=Path, default=REPO / 'server' / 'src' / 'srv_vfs.cpp')
    ap.add_argument('--out', type=Path, help='build and fake-game folder (default: a new temporary folder)')
    a = ap.parse_args()

    out = (a.out or Path(tempfile.mkdtemp(prefix='vfs-test-'))).resolve()
    build = out / 'build'
    game = out / 'game'
    for p in (build, game):
        if p.exists():
            shutil.rmtree(p)
        p.mkdir(parents=True)
    for rel, text in FILES.items():
        f = game / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text + '\n', encoding='ascii')

    shutil.copyfile(a.vfs_src, build / 'srv_vfs.cpp')
    shutil.copyfile(HERE / 'vfs_test_common.h', build / 'srv_common.h')
    shutil.copyfile(HERE / 'vfs_test_common.h', build / 'vfs_test_common.h')
    shutil.copyfile(HERE / 'test_vfs_resolve.cpp', build / 'test_vfs_resolve.cpp')

    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m, flush=True), out / 'vcvars-env.json')
    exe = build / 'test_vfs_resolve.exe'
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/Od', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS', f'/Fe{exe}', f'/Fo{build}\\',
           str(build / 'srv_vfs.cpp'), str(build / 'test_vfs_resolve.cpp'), '/link', 'kernel32.lib']
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(build))
    print(f'compile+link: rc={cp.returncode} ({a.vfs_src})')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0 or not exe.exists():
        print('run_vfs_test: BUILD FAILED')
        return 2
    rp = subprocess.run([str(exe), str(game)], capture_output=True, text=True, errors='replace')
    print(rp.stdout.rstrip())
    if rp.stderr.strip():
        print(rp.stderr.rstrip())
    print(f'run_vfs_test: {"OK" if rp.returncode == 0 else "FAILED"} (exit {rp.returncode}); folder {out}')
    return 0 if rp.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
