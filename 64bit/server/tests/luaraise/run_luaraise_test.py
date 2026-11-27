"""Ticket 4804 (speedup P2): offline tests of proxy/include/vp_luaraise.h, kRtE_LuaRaise's codec and the proxy's EXE-side
sequence (CreateArgs, pushes, Call*, DestroyArgs), driven with a fake script system. No game, no DLL.

    python -I perf/quietlock/quietlock.py run --who <id> --what "4804 luaraise tests" -- \
        python -I server/tests/luaraise/run_luaraise_test.py [--out DIR]

MSVC cl (vcvarsall; COMMON rule 9) compiles server/tests/luaraise/test_luaraise.cpp with ipc/src/marshal.cpp three times:
x64 (/W4 /WX /O2: the server's width), x86 (/W4 /WX /O2: the proxy's width; the proxy compiles the same header in
proxy_lua.cpp) and x64 /fsanitize=address (/Od /Zi /Oy-), plus a canary that must produce an ASan heap-buffer-overflow
report. Every binary runs under shell/tools/memcap.py (2048 MB). Exit 0 when the canary is caught and all three runs
report "0 failed" with exit 0 and no ASan report; 1 otherwise. Default --out: build/luaraise-tests (ignored by git).
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
VCVARSALL = pathlib.Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''


def vcvars_env(arch):
    venv = dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')  # bug 0108: the telemetry step hangs here
    out = subprocess.run(f'call "{VCVARSALL}" {arch} >nul && set', shell=True, capture_output=True, text=True, env=venv)
    if out.returncode:
        sys.exit(f'run_luaraise_test: vcvarsall {arch} failed:\n{out.stdout}\n{out.stderr}')
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=pathlib.Path, default=ROOT / 'build' / 'luaraise-tests')
    ap.add_argument('--mem-cap-mb', type=int, default=2048)
    a = ap.parse_args()
    sys.path.insert(0, str(ROOT / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the vp_luaraise tests (server/tests/luaraise/run_luaraise_test.py)', None)
    except ImportError:
        print('run_luaraise_test: warning: perf/quietlock/gate.py not found; going on', file=sys.stderr)
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    common = ['/nologo', '/EHsc', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
              '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'proxy' / 'include'), '/I' + str(ROOT / 'ipc' / 'include'),
              '/I' + str(ROOT / 'ipc' / 'src')]
    srcs = [ROOT / 'server' / 'tests' / 'luaraise' / 'test_luaraise.cpp', ROOT / 'ipc' / 'src' / 'marshal.cpp']
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    builds = [
        ('x64', 'test_luaraise_x64.exe', srcs, common + ['/O2']),
        ('x86', 'test_luaraise_x86.exe', srcs, common + ['/O2']),
        ('x64', 'test_luaraise_asan.exe', srcs, common + ['/Od', '/Zi', '/Oy-', '/fsanitize=address']),
        ('x64', 'asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
    ]
    envs = {}
    for arch, exe, files, fl in builds:
        if arch not in envs:
            envs[arch] = vcvars_env(arch)
        env = envs[arch]
        cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
        if not cl:
            sys.exit(f'run_luaraise_test: cl.exe not found in the vcvarsall {arch} PATH')
        objdir = out / ('obj-' + exe.replace('.exe', ''))
        objdir.mkdir(exist_ok=True)
        cmd = [cl] + fl + [str(s) for s in files] + ['/Fe' + str(out / exe), '/Fo' + str(objdir) + '\\',
                                                     '/Fd' + str(out / (exe + '.pdb'))]
        print('run_luaraise_test: compile', exe, flush=True)
        cp = subprocess.run(cmd, cwd=str(out), env=env, capture_output=True, text=True, errors='replace')
        if cp.returncode:
            print(cp.stdout + cp.stderr)
            print(f'run_luaraise_test: FAIL: compile {exe} exited {cp.returncode}')
            return 1
    memcap = [sys.executable, '-I', str(ROOT / 'shell' / 'tools' / 'memcap.py'), '--mb', str(a.mem_cap_mb), '--']
    cp = subprocess.run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=envs['x64'], capture_output=True,
                        text=True, errors='replace')
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_luaraise_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'run_luaraise_test: canary caught (exit {cp.returncode}, heap-buffer-overflow reported)', flush=True)
    ok = True
    for arch, exe in (('x64', 'test_luaraise_x64.exe'), ('x86', 'test_luaraise_x86.exe'),
                      ('x64', 'test_luaraise_asan.exe')):
        cp = subprocess.run(memcap + [str(out / exe)], cwd=str(out), env=envs[arch], capture_output=True, text=True,
                            errors='replace')
        text = (cp.stdout + cp.stderr).rstrip()
        print(f'--- {exe} (exit {cp.returncode})')
        print(text)
        if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
            print(f'run_luaraise_test: FAIL: {exe}')
            ok = False
    print('run_luaraise_test: ' + ('OK' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
