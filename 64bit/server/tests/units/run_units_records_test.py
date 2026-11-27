"""Tests of the unit feed's records 3-6 (server/src/units/srv_units_records.cpp; issue 4457, epic 0375, 2026-11-16).
Standalone build (the codec depends on srv_feed.h only). Pattern of run_units_seed_test.py (issue 4453).

    set C5_JOB_OWNER=0375
    python -I perf/quietlock/quietlock.py run --who 0375 --what "4457 records tests" -- \
        python -I server/tests/units/run_units_records_test.py --out DIR [--asan]

1. clang-cl x64 (/std:c++14 /W4 /WX /O2 /MD) builds test_units_records.cpp with srv_units_records.cpp into
   DIR/test_units_records.exe and runs it: every check must pass (exit 0, "0 failed").
2. Negative tests: the build with -DRECORDS_TEST_INJECT_FAIL must exit 1 with a FAIL line; `--decode` of record files
   written here must print the named refusal and exit 10 + the code (Truncated 11, WrongSize 12, BadRecord 13,
   UnitsDiffBeforeFull 14, LoopBack 15, OlderFormat 16, UnknownType 17); a missing file and a bad argument exit 2; a good
   file prints "records: OK <n> records" and exits 0.
3. --asan: MSVC cl /fsanitize=address builds a canary (must be caught) and the tests (0 reports, exit 0), under
   shell/tools/memcap.py (2048 MB job cap).
Exit 0 when every part passes, 1 when one fails, 2 when a build fails. Python standard library.
"""
import argparse
import importlib.util
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = [REPO / 'server' / 'src' / 'units' / 'srv_units_records.cpp', HERE / 'test_units_records.cpp']
INC = ['/I' + str(REPO / 'server' / 'src')]
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''
NOWIN = 0x08000000  # CREATE_NO_WINDOW


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)


def clang_build(x64build, env, out, exe, extra):
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + INC + extra + [f'/Fe{out / exe}', f'/Fo{out}\\'] + \
          [str(s) for s in SRC] + ['/link', 'kernel32.lib']
    cp = run(cmd, env=env, cwd=str(out))
    print(f'compile {exe}: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-4000:])
    return cp.returncode == 0 and (out / exe).exists()


def vcvars_env():
    cp = subprocess.run(f'call "{VCVARSALL}" x64 >nul && set', shell=True, capture_output=True, text=True,
                        env=dict(__import__('os').environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if cp.returncode:
        return None
    env = {}
    for line in cp.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def asan(out):
    env = vcvars_env()
    if not env:
        print('run_units_records_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_units_records_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS'] + INC
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_units_records_asan.exe', SRC, flags)):
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out) + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))]
        cp = run(cmd, cwd=str(out), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = run(memcap + [str(out / 'asan_canary.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_units_records_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    fails = 0
    for args in ([], ['--decode', str(out / 'rec_good.bin')], ['--decode', str(out / 'rec_trunc.bin')]):
        cp = run(memcap + [str(out / 'test_units_records_asan.exe')] + args, cwd=str(out), env=env)
        text = cp.stdout + cp.stderr
        if not args:
            ok = cp.returncode == 0 and ' 0 failed' in text and 'AddressSanitizer' not in text
            print(text.rstrip()[-1500:])
        else:
            ok = 'AddressSanitizer' not in text and ('records: OK' in text or 'REFUSED Truncated' in text)
        print(f'asan {" ".join(args) or "tests"}: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
        fails += not ok
    return 1 if fails else 0


def rec(rtype, payload):
    return struct.pack('<II', rtype, len(payload)) + payload


def unit(owner, uid, plot=5, seq=1):
    # UnitRecord: i8 owner, i8 domain, i8 facing, u8 flags, i32 id, i32 unitType, i32 plot, i16 damage, maxHP, artStyle,
    # era, u32 seq, u32 reserved = 32 B
    return struct.pack('<bbbBiiihhhhII', owner, 1, 2, 8, uid, 3, plot, 0, 100, 1, 0, seq, 0)


def write_cases(out):
    """name -> (bytes, format, expected exit, expected text)"""
    good = (rec(3, struct.pack('<QIIII', 1, 1, 1, 1, 2) + unit(0, 1001) + unit(1, 1002)) +
            rec(4, struct.pack('<QIII', 1, 1, 7, 2) + struct.pack('<ii', 10, 11)) +
            rec(6, struct.pack('<QII', 2, 2, 0)) +
            rec(3, struct.pack('<QIIII', 2, 2, 1, 0, 0) + struct.pack('<I', 1) + struct.pack('<ii', 1, 1002)))
    cases = {
        'rec_good.bin': (good, 4, 0, 'records: OK 4 records'),
        'rec_trunc.bin': (good[:-3], 4, 11, 'records: REFUSED Truncated record 3: 36 payload bytes, 33 left'),
        'rec_size.bin': (rec(4, struct.pack('<QI', 1, 0) + b'\0'), 4, 12, 'REFUSED WrongSize record 0: PATHS: 1 bytes after its 0 paths'),
        'rec_caller.bin': (rec(6, struct.pack('<QII', 1, 5, 0)), 4, 13, 'REFUSED BadRecord record 0: PLAYERS: caller 5'),
        'rec_diff.bin': (rec(3, struct.pack('<QIIIII', 1, 2, 1, 0, 0, 0)), 4, 14,
                         'REFUSED UnitsDiffBeforeFull record 0: UNITS: a diff before any full table'),
        'rec_loop.bin': (rec(4, struct.pack('<QI', 5, 0)) + rec(4, struct.pack('<QI', 4, 0)), 4, 15,
                         'REFUSED LoopBack record 1: PATHS loopIter 4 below the previous record\'s 5'),
        'rec_older.bin': (rec(4, struct.pack('<QI', 1, 0)), 3, 16,
                          'REFUSED OlderFormat record 0: record type 4 PATHS needs format_version 4; the bundle has 3'),
        'rec_type.bin': (rec(7, struct.pack('<QI', 1, 0)), 4, 17, 'REFUSED UnknownType record 0: record type 7'),
        'rec_remove.bin': (rec(3, struct.pack('<QIIII', 1, 1, 1, 1, 1) + unit(0, 1001)) +
                           rec(3, struct.pack('<QIIIII', 2, 2, 1, 0, 0, 1) + struct.pack('<ii', 0, 1009)), 4, 13,
                           'REFUSED BadRecord record 1: UNITS remove: key (0, 1009) not held by the previous table'),
    }
    for name, (data, _v, _rc, _t) in cases.items():
        (out / name).write_bytes(data)
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the unit records tests (server/tests/units/run_units_records_test.py)', None)
    except ImportError:
        print('run_units_records_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not clang_build(x64build, env, out, 'test_units_records.exe', []):
        print('run_units_records_test: BUILD FAILED')
        return 2
    exe = str(out / 'test_units_records.exe')
    cp = run([exe])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_units_records.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    if not clang_build(x64build, env, out, 'test_units_records_inject.exe', ['/DRECORDS_TEST_INJECT_FAIL']):
        print('run_units_records_test: BUILD FAILED (inject)')
        return 2
    cp = run([str(out / 'test_units_records_inject.exe')])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_units_records_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    fails += not neg
    cases = write_cases(out)
    for name, (_data, ver, rc, text) in cases.items():
        cp = run([exe, '--decode', str(out / name), '--format', str(ver)])
        ok = cp.returncode == rc and text in cp.stdout
        print(f'decode {name} (format {ver}): exit {cp.returncode} -> {"PASS" if ok else "FAIL"}: {cp.stdout.strip()}')
        fails += not ok
    for args, text in ((['--decode', str(out / 'no_such_file.bin')], 'cannot open'), (['--bogus'], 'usage')):
        cp = run([exe] + args)
        ok = cp.returncode == 2 and text in cp.stdout
        print(f'negative {args}: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
        fails += not ok
    if a.asan:
        r = asan(out)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_units_records_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
