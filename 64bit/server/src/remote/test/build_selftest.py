"""Build vp_selftest.exe (the remote-mode self-test client, task 2303) against a build_server.py --out tree.

    python server/src/remote/test/build_selftest.py --out <build_server --out dir>

Compiles vp_selftest.cpp (x64, the server's clang-cl and vcvars environment) and links it with the vpipc objects and
vp_ids.gen.obj that build_server.py left in <out>/server/obj; writes <out>/bin/vp_selftest.exe. Run it with
`vp_selftest.exe <out>/bin/server.exe <remote server.cfg> [<game database> [init]]` (the config from
`server/run_server.py --remote-config`).
"""
import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
x64build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x64build)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    out = a.out.resolve()
    sdir = out / 'server'
    tdir = sdir / 'selftest'
    tdir.mkdir(parents=True, exist_ok=True)

    def say(msg, echo=True):
        if echo:
            print(msg, flush=True)
    env = x64build.load_vcvars_env(say, sdir / 'vcvars-env.json')
    obj = tdir / 'vp_selftest.obj'
    cmd = [str(x64build.CLANG_CL), '-m64', '/c', '/nologo', '/MD', '/EHsc', '/Zi', '/std:c++14', '/W3', '/WX', '/O2',
           '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', f'/I{REPO / "ipc" / "include"}',
           f'/I{REPO / "proxy" / "include"}', f'/I{out / "gen" / "common"}', f'/Fo{obj}', str(HERE / 'vp_selftest.cpp')]
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
    print((cp.stdout + cp.stderr).rstrip())
    if cp.returncode:
        sys.exit('build_selftest: compile failed')
    exe = out / 'bin' / 'vp_selftest.exe'
    objs = [obj] + sorted((sdir / 'obj' / 'vpipc').glob('*.obj')) + [sdir / 'obj' / 'gen' / 'vp_ids.gen.obj']
    cmd = [str(x64build.LLD_LINK), f'/OUT:{exe}', '/MACHINE:X64', '/SUBSYSTEM:CONSOLE', '/DEBUG'] + \
        [str(o) for o in objs] + ['kernel32.lib']
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
    print((cp.stdout + cp.stderr).rstrip())
    if cp.returncode:
        sys.exit('build_selftest: link failed')
    print(f'build_selftest: {exe}')


if __name__ == '__main__':
    main()
