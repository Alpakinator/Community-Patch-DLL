"""Unit tests of the formation-seed allocator (server/src/units/srv_units_seed.cpp; issue 4453, epic 0375, 2026-11-15).
Standalone: the allocator's build into server.exe is issue 4454's build_server.py change. Pattern of
server/tests/feed/run_feed_test.py (issue 3303).

    python -I perf/quietlock/quietlock.py run --who 0375 --what "seed allocator tests" --         python -I server/tests/units/run_units_seed_test.py --out DIR [--asan]

1. clang-cl x64 (/std:c++14 /W4 /WX /O2 /MD) builds server/tests/units/test_units_seed.cpp with srv_units_seed.cpp into
   DIR/test_units_seed.exe and runs it: every check must pass (exit 0, "0 failed").
2. Negative tests: the build with -DSEED_TEST_INJECT_FAIL must exit 1 with a FAIL line; `--replay` of a missing file and
   of a refused line, and an unknown argument, must exit 2 naming them; a small replay must print the expected table.
   Issue 4462: the replays of 4458's recorded feeds (server/evidence/0375/4462/*_replay.txt) must give every unit of
   every check the reader's index (899 / 899, 34 / 34; issue 4469 adds its era run, ../4469/r1_replay.txt, 369 / 369); an X check with a wrong index must exit 1 naming the units, a
   malformed X or H line must be refused (exit 2).
3. --asan: MSVC cl (vcvarsall x64; the clang-cl ASan runtime of LLVM 19 crashes at start-up here, render/README.md)
   builds the tests with /fsanitize=address /Zi /Oy- /W4 /WX into DIR/test_units_seed_asan.exe and a canary (one byte past a
   heap block); the canary must be caught (heap-buffer-overflow), then the tests and the two recorded replays must pass
   with no ASan report. All
   run under shell/tools/memcap.py (2048 MB job cap).
Exit 0 when every part passes, 1 when one fails, 2 when a build fails. Python standard library.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC = [REPO / 'server' / 'src' / 'units' / 'srv_units_seed.cpp', HERE / 'test_units_seed.cpp']
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''
NOWIN = 0x08000000  # CREATE_NO_WINDOW
NL = chr(10)
# issue 4462: 4458's recorded feeds in the replay form (server/evidence/0375/4462/feed_to_replay.py) and the line the
# allocator must print for each (every unit of every check equal to the reader's EXE index)
EVIDENCE = REPO / 'server' / 'evidence' / '0375' / '4462'
RECORDED = [('c3_replay.txt', 'X total checks 17 units 899 equal 899'),
            ('er_replay.txt', 'X total checks 1 units 34 equal 34'),
            # issue 4469: the CAT_0009 copy's recorded feed through three era changes of Polynesia (embarked units
            # re-created by Embark(true), land workers by Created; 4469's run r1, unit-formations.md 12.7.11)
            ('../4469/r1_replay.txt', 'X total checks 11 units 369 equal 369')]


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)


def clang_build(x64build, env, out, exe, extra):
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + extra + [f'/Fe{out / exe}', f'/Fo{out}\\'] + \
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
        print('run_units_seed_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_units_seed_test: ASan: cl.exe not found in the vcvarsall x64 PATH')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    # /W4 without /WX here: the clang-cl build above is the warning gate (/W4 /WX); MSVC's own warnings are printed
    flags = ['/nologo', '/EHsc', '/W4', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS']
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_units_seed_asan.exe', SRC, flags)):
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
        print(f'run_units_seed_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = run(memcap + [str(out / 'test_units_seed_asan.exe')], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    print(text.rstrip()[-4000:])
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'run_units_seed_test: FAIL: ASan tests exit {cp.returncode}')
        return 1
    print('asan tests: OK (exit 0, no AddressSanitizer report)')
    # issue 4462: the recorded replays under ASan too (the allocator's group, late passes and era / paradrop paths)
    for name, want in RECORDED:
        cp = run(memcap + [str(out / 'test_units_seed_asan.exe'), '--replay', str(EVIDENCE / name)], cwd=str(out), env=env)
        text = cp.stdout + cp.stderr
        if cp.returncode != 0 or 'AddressSanitizer' in text or want not in text:
            print(text[-3000:])
            print(f'run_units_seed_test: FAIL: ASan replay {name} exit {cp.returncode}')
            return 1
        print(f'asan replay {name}: OK ({want}, no AddressSanitizer report)')
    return 0


def recorded_replays(exe, out):
    """Issue 4462: the allocator on 4458's recorded feeds against the reader (899 / 899 and 34 / 34), and the X check's
    own negatives (a wrong index fails with exit 1 and names the unit; a malformed X or H line is refused, exit 2)."""
    fails = 0
    for name, want in RECORDED:
        cp = run([str(exe), '--replay', str(EVIDENCE / name)])
        ok = cp.returncode == 0 and want in cp.stdout.splitlines() and 'outOfStep 0' in cp.stdout
        print(f'recorded replay {name}: exit {cp.returncode}, '
              f'{[l for l in cp.stdout.splitlines() if l.startswith("X total")]} -> {"PASS" if ok else "FAIL"}')
        if not ok:
            print(cp.stdout[-3000:])
        fails += not ok
    base = ['H 4.3 3.9 0.6 0.35', 'E 1 0.0 11 0 1000 0 0 5', 'E 2 0.0 11 0 1001 0 0 5', 'E 3 1.0 15 0 1000 0 0 -1',
            'E 4 1.0 14 0 1000 0 0 5', 'E 5 1.0 11 1 7 0 0 9']
    cases = [('x_wrong.txt', base + ['X wrong 3 0 1001 1 1 7 0 0 1000 0', ''], 1,
              ['X wrong units 3 equal 1 diffs [1 7 0 2] [0 1000 0 -1]', 'X total checks 1 units 3 equal 1']),
             ('x_right.txt', base + ['X right 2 0 1001 1 1 7 2', ''], 0, ['X right units 2 equal 2 diffs']),
             ('x_extra.txt', base + ['X extra 1 0 1001 1 1 7 2', ''], 2, ['replay: refused line 7: X extra']),
             ('x_short.txt', base + ['X short 2 0 1001 1', ''], 2, ['replay: refused line 7: X short']),
             ('h_short.txt', ['H 4.3 3.9', ''], 2, ['replay: refused line 1: H 4.3 3.9'])]
    for fname, text, rc, wants in cases:
        p = out / fname
        p.write_text(NL.join(text), encoding='ascii')
        cp = run([str(exe), '--replay', str(p)])
        lines = cp.stdout.splitlines()
        ok = cp.returncode == rc and all(any(l.startswith(w) for l in lines) for w in wants)
        print(f'replay check {fname}: exit {cp.returncode} (want {rc}) -> {"PASS" if ok else "FAIL"}')
        if not ok:
            print(cp.stdout[-2000:])
        fails += not ok
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the seed allocator tests (server/tests/units/run_units_seed_test.py)', None)
    except ImportError:
        print('run_units_seed_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0
    if not clang_build(x64build, env, out, 'test_units_seed.exe', []):
        print('run_units_seed_test: BUILD FAILED')
        return 2
    cp = run([str(out / 'test_units_seed.exe')])
    print(cp.stdout.rstrip())
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout
    print(f'test_units_seed.exe: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    # negative: an injected failing check must fail the binary
    if not clang_build(x64build, env, out, 'test_units_seed_inject.exe', ['/DSEED_TEST_INJECT_FAIL']):
        print('run_units_seed_test: BUILD FAILED (inject)')
        return 2
    cp = run([str(out / 'test_units_seed_inject.exe')])
    neg = cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout
    print(f'test_units_seed_inject.exe (negative): exit {cp.returncode} -> {"PASS" if neg else "FAIL"}')
    if not neg:
        print('run_units_seed_test: a failing check did not fail the binary')
        print(cp.stdout[-2000:])
    fails += not neg
    # negative: the replay mode's refusals
    bad = out / 'replay_bad.txt'
    bad.write_text(NL.join(['H 1 2 0.5', 'E 1 0.0 11 0 1000 0 0', 'Q nonsense', '']), encoding='ascii')
    cases = [([str(out / 'test_units_seed.exe'), '--replay', str(out / 'no_such_file.txt')], 2, 'cannot open'),
             ([str(out / 'test_units_seed.exe'), '--replay', str(bad)], 2, 'refused line 3'),
             ([str(out / 'test_units_seed.exe'), '--bogus'], 2, 'usage')]
    for cmd, rc, text in cases:
        cp = run(cmd)
        ok = cp.returncode == rc and text in cp.stdout
        print(f'negative {cmd[1:]}: exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
        fails += not ok
    good = out / 'replay_good.txt'
    good.write_text(NL.join(['E 1 0.0 11 0 1000 0 0', 'E 2 0.0 11 0 1001 0 0', 'E 3 0.0 14 0 1000 0 0', 'R one', '']),
                    encoding='ascii')
    cp = run([str(out / 'test_units_seed.exe'), '--replay', str(good)])
    lines = cp.stdout.splitlines()
    ok = cp.returncode == 0 and 'R one size 2 live 1 pending 0' in lines and 'L 1 0 1001 1' in lines and 'F 0' in lines
    print(f'replay (positive): exit {cp.returncode} -> {"PASS" if ok else "FAIL"}')
    fails += not ok
    fails += recorded_replays(out / 'test_units_seed.exe', out)
    if a.asan:
        r = asan(out)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_units_seed_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
