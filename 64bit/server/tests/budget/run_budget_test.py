"""Ticket 4652 (2026-11-16): host test of the turn budget's accounting, no game, no DLL, no fork source (about 15 s).

  python -I perf/quietlock/quietlock.py run -- python -I server/tests/budget/run_budget_test.py [--asan] [--out DIR]

Compiles server/tests/budget/test_budget.cpp with the server's SrvBudgetCore (server/src/remote/srv_budget_core.h), the
proxy's ProxyBudgetCore (proxy/src/proxy_budget_core.h) and the shared classes and switch (proxy/include/vp_budget.h):
  default  clang-cl x64, the server's toolchain (tools/x64-dll/build.py), /W3 /WX /O2
  --asan   in addition MSVC cl with /fsanitize=address, x64 and x86 (the proxy is x86), each after a canary (one byte
           written past a heap block) that must be reported, else the run fails ("ASan not active"); COMMON rule 9.
           MSVC, not clang-cl: LLVM 19's clang-cl ASan runtime crashes at start-up on this machine (options/test_asan.py).
Each binary runs under shell/tools/memcap.py (2048 MB). Exit 0 when every build passes every check; 1 when a check or the
canary fails; 2 when a build fails. Default --out: a new temporary folder.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
INCLUDES = [REPO / 'server' / 'src' / 'remote', REPO / 'proxy' / 'src', REPO / 'proxy' / 'include']
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def vcvars_env(arch):
    venv = dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')   # bug 0108: the telemetry step hangs on this workstation
    out = subprocess.run(f'call "{VCVARSALL}" {arch} >nul && set', shell=True, capture_output=True, text=True, env=venv)
    if out.returncode:
        raise SystemExit(f'run_budget_test: vcvarsall {arch} failed:\n{out.stdout}\n{out.stderr}')
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def run(exe, env, cwd):
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = subprocess.run(memcap + [str(exe)], cwd=str(cwd), env=env, capture_output=True, text=True, errors='replace',
                        creationflags=0x08000000)
    return cp.returncode, cp.stdout + cp.stderr


def compile_(cmd, env, cwd, what):
    cp = subprocess.run(cmd, cwd=str(cwd), env=env, capture_output=True, text=True, errors='replace')
    if cp.returncode:
        print((cp.stdout + cp.stderr).rstrip())
        print(f'run_budget_test: BUILD FAILED: {what} (exit {cp.returncode})')
        return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--asan', action='store_true', help='also MSVC /fsanitize=address builds, x64 and x86')
    ap.add_argument('--out', type=Path, help='build folder (default: a new temporary folder)')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the turn budget host test (server/tests/budget/run_budget_test.py)', None)
    except ImportError:
        print('run_budget_test: warning: perf/quietlock/gate.py not found; going on', file=sys.stderr)
    out = (a.out or Path(tempfile.mkdtemp(prefix='budget-test-'))).resolve()
    out.mkdir(parents=True, exist_ok=True)
    src = HERE / 'test_budget.cpp'
    inc = [f'/I{p}' for p in INCLUDES]
    results = []

    # 1. clang-cl x64, the server's toolchain
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m, flush=True), out / 'vcvars-env.json')
    exe = out / 'test_budget_clang_x64.exe'
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX'] + inc + \
          [f'/Fe{exe}', f'/Fo{out}\\', str(src), '/link', 'kernel32.lib']
    if not compile_(cmd, env, out, 'clang-cl x64'):
        return 2
    rc, text = run(exe, env, out)
    print(text.rstrip())
    results.append(('clang-cl x64', rc == 0 and ' 0 failed' in text))

    if a.asan:
        (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
        for arch in ('x64', 'x86'):
            venv = vcvars_env(arch)
            cl = shutil.which('cl', path=venv.get('PATH', venv.get('Path', '')))
            if not cl:
                print(f'run_budget_test: cl.exe not found in the vcvarsall {arch} PATH')
                return 2
            canary = out / f'asan_canary_{arch}.exe'
            if not compile_([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', str(out / 'asan_canary.cpp'),
                             f'/Fe{canary}', f'/Fo{out}\\', f'/Fd{canary}.pdb'], venv, out, f'canary {arch}'):
                return 2
            crc, ctext = run(canary, venv, out)
            if crc == 0 or 'heap-buffer-overflow' not in ctext:
                print(ctext[-2000:])
                print(f'run_budget_test: FAIL: ASan not active on {arch} (canary exit {crc})')
                return 1
            print(f'run_budget_test: {arch} canary caught (exit {crc}, heap-buffer-overflow reported)')
            exe = out / f'test_budget_asan_{arch}.exe'
            cmd = [cl, '/nologo', '/EHsc', '/W4', '/WX', '/wd4127', '/wd4324', '/wd4245', '/Zi', '/Oy-', '/fsanitize=address',
                   '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'] + inc + \
                  [str(src), f'/Fe{exe}', f'/Fo{out}\\', f'/Fd{exe}.pdb']
            if not compile_(cmd, venv, out, f'MSVC ASan {arch}'):
                return 2
            rc, text = run(exe, venv, out)
            print(text.rstrip())
            results.append((f'MSVC /fsanitize=address {arch}', rc == 0 and ' 0 failed' in text
                            and 'AddressSanitizer' not in text))
            if arch == 'x86':  # the proxy is x86: its cost per call from an optimised x86 build (no ASan)
                exe = out / 'test_budget_msvc_x86_O2.exe'
                cmd = [cl, '/nologo', '/EHsc', '/W4', '/WX', '/wd4127', '/wd4324', '/wd4245', '/O2', '/DWIN32_LEAN_AND_MEAN',
                       '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'] + inc + [str(src), f'/Fe{exe}', f'/Fo{out}\\']
                if not compile_(cmd, venv, out, 'MSVC x86 /O2'):
                    return 2
                rc, text = run(exe, venv, out)
                print(text.rstrip())
                results.append(('MSVC x86 /O2', rc == 0 and ' 0 failed' in text))
    for what, ok in results:
        print(f'run_budget_test: {what}: {"PASS" if ok else "FAIL"}')
    ok = all(r for _, r in results)
    print(f'run_budget_test: {"OK" if ok else "FAILED"}; folder {out}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
