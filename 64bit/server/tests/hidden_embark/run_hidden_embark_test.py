"""Ticket 0573 (D193): host test of the server's hidden-embark switch, no game, no DLL (about 10 s).

  python -I perf/quietlock/quietlock.py run -- python -I server/tests/hidden_embark/run_hidden_embark_test.py [--asan] [--out DIR]

Compiles server/tests/hidden_embark/test_hidden_embark.cpp with server/src/srv_hidden_embark.cpp and the options library
(options/src/vp64_options.cpp); SrvOptions() and SrvExeCrash::ReportServerFailure are stubs in the test:
  default  clang-cl x64, the server's toolchain (tools/x64-dll/build.py), /W3 /WX /O2
  --asan   in addition MSVC cl x64 with /fsanitize=address, after a canary (one byte written past a heap block) that
           must be reported, else the run fails ("ASan not active"); COMMON rule 9.
Each binary runs under shell/tools/memcap.py (2048 MB). Exit 0 when every build passes every check; 1 when a check or the
canary fails; 2 when a build fails. Default --out: a new temporary folder. Pattern: server/tests/budget/run_budget_test.py.
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
INCLUDES = [REPO / 'server' / 'src', REPO / 'server' / 'src' / 'remote', REPO / 'options' / 'include']
SOURCES = [HERE / 'test_hidden_embark.cpp', REPO / 'server' / 'src' / 'srv_hidden_embark.cpp',
           REPO / 'options' / 'src' / 'vp64_options.cpp']


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--asan', action='store_true', help='also an MSVC /fsanitize=address build (x64)')
    ap.add_argument('--out', type=Path, help='build folder (default: a new temporary folder)')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the hidden-embark host test (server/tests/hidden_embark/run_hidden_embark_test.py)', None)
    except ImportError:
        print('run_hidden_embark_test: warning: perf/quietlock/gate.py not found; going on', file=sys.stderr)
    bt = load('run_budget_test', REPO / 'server' / 'tests' / 'budget' / 'run_budget_test.py')
    out = (a.out or Path(tempfile.mkdtemp(prefix='hidden-embark-test-'))).resolve()
    out.mkdir(parents=True, exist_ok=True)
    inc = [f'/I{p}' for p in INCLUDES]
    srcs = [str(s) for s in SOURCES]
    results = []

    x64build = bt.load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m, flush=True), out / 'vcvars-env.json')
    exe = out / 'test_hidden_embark_clang_x64.exe'
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX'] + inc + \
          [f'/Fe{exe}', f'/Fo{out}\\'] + srcs + ['/link', 'kernel32.lib', 'user32.lib', 'shell32.lib', 'ole32.lib']
    if not bt.compile_(cmd, env, out, 'clang-cl x64'):
        return 2
    rc, text = bt.run(exe, env, out)
    print(text.rstrip())
    results.append(('clang-cl x64', rc == 0 and ' 0 failed' in text))

    if a.asan:
        (out / 'asan_canary.cpp').write_text(bt.CANARY.replace('#include <stdio.h>', '#include <stdio.h>\n#include <stdlib.h>'),
                                             encoding='ascii')
        venv = bt.vcvars_env('x64')
        cl = shutil.which('cl', path=venv.get('PATH', venv.get('Path', '')))
        if not cl:
            print('run_hidden_embark_test: cl.exe not found in the vcvarsall x64 PATH')
            return 2
        canary = out / 'asan_canary_x64.exe'
        if not bt.compile_([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', str(out / 'asan_canary.cpp'),
                            f'/Fe{canary}', f'/Fo{out}\\', f'/Fd{canary}.pdb'], venv, out, 'canary x64'):
            return 2
        crc, ctext = bt.run(canary, venv, out)
        if crc == 0 or 'heap-buffer-overflow' not in ctext:
            print(ctext[-2000:])
            print(f'run_hidden_embark_test: FAIL: ASan not active on x64 (canary exit {crc})')
            return 1
        print(f'run_hidden_embark_test: x64 canary caught (exit {crc}, heap-buffer-overflow reported)')
        exe = out / 'test_hidden_embark_asan_x64.exe'
        cmd = [cl, '/nologo', '/EHsc', '/W4', '/WX', '/wd4127', '/Zi', '/Oy-', '/fsanitize=address',
               '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'] + inc + srcs + \
              [f'/Fe{exe}', f'/Fo{out}\\', f'/Fd{exe}.pdb', '/link', 'user32.lib', 'shell32.lib', 'ole32.lib']
        if not bt.compile_(cmd, venv, out, 'MSVC ASan x64'):
            return 2
        rc, text = bt.run(exe, venv, out)
        print(text.rstrip())
        ok = rc == 0 and ' 0 failed' in text and 'AddressSanitizer' not in text
        results.append(('MSVC ASan x64', ok))

    for name, ok in results:
        print(f'run_hidden_embark_test: {name}: {"PASS" if ok else "FAIL"}')
    return 0 if all(ok for _, ok in results) else 1


if __name__ == '__main__':
    sys.exit(main())
