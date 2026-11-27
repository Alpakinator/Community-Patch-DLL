"""Build the issue 1005 libraries, link stub_selftest.cpp against them and run it (x64, /MD).

    python run_selftest.py --out DIR [--config debug|release]

Passes when the self-test exits 0 and the log it leaves has a second COUNT block written by the exit flush
(DLL unload / process exit path), i.e. the file ends with "#  flush 2 ...".
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import build_stubs as bs  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source: git checkout at the deps/dll.lock commit or a plain copy of it')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit')
    a = ap.parse_args()
    try:
        a.src = bs.x64build.resolve_src(a.src, a.unlocked)['src']
    except bs.x64build.SrcError as e:
        sys.exit(f'run_selftest: FAILED: {e}')
    out = a.out.resolve()
    libs = out / 'libs'
    rc = subprocess.call([sys.executable, str(HERE.parent / 'build_stubs.py'), '--out', str(libs), '--config', a.config,
                          '--src', str(a.src), '--groups', 'lua51']
                         + (['--unlocked'] if a.unlocked else []))
    if rc != 0:
        sys.exit('build_stubs failed')
    env = bs.vcvars_env()
    obj = out / 'stub_selftest.obj'
    args = bs.cpp_args(a.src, a.config, 'MD')
    args = [x for x in args if x != '/Zl']  # the test is the final image: keep the default CRT records
    _, rc, text = bs.compile_one(env, args, HERE / 'stub_selftest.cpp', obj)
    print(text)
    if rc != 0:
        sys.exit('compile failed')
    exe = out / 'stub_selftest.exe'
    cmd = [str(bs.LLD_LINK), '/nologo', '/machine:x64', '/subsystem:console', '/debug', f'/out:{exe}', str(obj)]
    cmd += [str(libs / f'{g}.lib') for g in ('lua51',)]
    # Lua is the shared lua51_x64.dll since issue 1009: link its import library, run with the DLL beside the exe.
    if (libs / 'lua51_x64.lib').exists():
        cmd.append(str(libs / 'lua51_x64.lib'))
        shutil.copy2(libs / 'lua51_x64.dll', out / 'lua51_x64.dll')
    cmd += ['kernel32.lib']
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    print((r.stdout + r.stderr).strip())
    if r.returncode != 0:
        sys.exit('link failed')
    log = out / 'stubcalls-selftest.log'
    if log.exists():
        log.unlink()
    rc = subprocess.call([str(exe)], env=dict(os.environ, CV64_STUBLOG=str(log)), cwd=str(out))
    text = log.read_text(encoding='utf-8') if log.exists() else ''
    print('---- ' + str(log))
    print(text)
    tail_ok = text.rstrip().splitlines()[-1].startswith('#\tflush 2\t') if text else False
    print(f'self-test exit {rc}; exit flush written: {tail_ok}')
    sys.exit(0 if rc == 0 and tail_ok else 1)


if __name__ == '__main__':
    main()
