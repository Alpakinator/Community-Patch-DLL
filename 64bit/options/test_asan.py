"""The options unit tests under AddressSanitizer (one-off 9042; COMMON rule 9: native code runs its suite once under
MSVC /fsanitize=address).

    python -I perf/quietlock/quietlock.py run --who <id> --what "options ASan" -- python options/test_asan.py [--out DIR]

MSVC cl (vcvarsall x64; LLVM 19's clang-cl ASan runtime crashes at start-up on this machine, render/README.md) compiles
options/src/vp64_options.cpp and options/tests/test_options.cpp with /fsanitize=address /Zi /Oy- /W4 /WX into
<out>/test_options_asan.exe, and a canary (one byte written past a heap block) into <out>/asan_canary.exe. The canary must
produce a heap-buffer-overflow report, else the run fails ("ASan not active"); then the tests run with the same
arguments as ctest's. Both run under shell/tools/memcap.py (job memory cap, default 2048 MB). Default --out:
build/asan-options (ignored by git). Exit 0 when the canary is caught and every test passes. Python standard library.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VCVARSALL = pathlib.Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''


def vcvars_env():
    # Bug 0108: vcvarsall's telemetry step starts a background PowerShell, which hangs on this workstation.
    venv = dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')
    out = subprocess.run(f'call "{VCVARSALL}" x64 >nul && set', shell=True, capture_output=True, text=True, env=venv)
    if out.returncode:
        sys.exit(f'test_asan: vcvarsall x64 failed:\n{out.stdout}\n{out.stderr}')
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=pathlib.Path, default=ROOT / 'build' / 'asan-options')
    ap.add_argument('--mem-cap-mb', type=int, default=2048)
    a = ap.parse_args()
    sys.path.insert(0, str(ROOT / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the options ASan tests (options/test_asan.py)', None)
    except ImportError:
        print('test_asan: warning: perf/quietlock/gate.py not found; going on', file=sys.stderr)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = vcvars_env()
    flags = ['/nologo', '/EHsc', '/W4', '/WX', '/utf-8', '/Zi', '/Oy-', '/fsanitize=address', '/DWIN32_LEAN_AND_MEAN',
             '/DNOMINMAX', '/D_WIN32_WINNT=0x0601', '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'options' / 'include')]
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    builds = [
        ('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
        ('test_options_asan.exe', [ROOT / 'options' / 'src' / 'vp64_options.cpp',
                                   ROOT / 'options' / 'tests' / 'test_options.cpp'], flags),
    ]
    # CreateProcess searches the caller's PATH, not env's: find cl in the vcvars PATH
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        sys.exit('test_asan: cl.exe not found in the vcvarsall x64 PATH')
    for exe, srcs, fl in builds:
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out) + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))]
        print('test_asan: compile', exe, flush=True)
        cp = subprocess.run(cmd, cwd=str(out), env=env, capture_output=True, text=True, errors='replace')
        if cp.returncode:
            print(cp.stdout + cp.stderr)
            print(f'test_asan: FAIL: compile {exe} exited {cp.returncode}')
            return 1
    memcap = [sys.executable, '-I', str(ROOT / 'shell' / 'tools' / 'memcap.py'), '--mb', str(a.mem_cap_mb), '--']
    print('test_asan: canary (must be caught)', flush=True)
    cp = subprocess.run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=env, capture_output=True, text=True,
                        errors='replace')
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'test_asan: FAIL: ASan not active (canary exit {cp.returncode}, no heap-buffer-overflow report)')
        return 1
    print(f'test_asan: canary caught (exit {cp.returncode}, heap-buffer-overflow reported)', flush=True)
    scratch = out / 'scratch'
    print('test_asan: test_options_asan.exe', flush=True)
    cp = subprocess.run(memcap + [str(out / 'test_options_asan.exe'), str(ROOT / 'docs' / 'OPTIONS.md'), str(scratch)],
                        cwd=str(out), env=env, capture_output=True, text=True, errors='replace')
    text = cp.stdout + cp.stderr
    print(text.rstrip())
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'test_asan: FAIL: tests exit {cp.returncode}')
        return 1
    print('test_asan: OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
