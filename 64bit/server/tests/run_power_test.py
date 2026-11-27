"""Bug 0118 (tracker 4000, 2026-11-13): the power-throttling opt-out of the servers (server/src/srv_power.h).

  python server/tests/run_power_test.py --out DIR [--server-exe EXE] [--rate SECONDS]

1. Builds server/tests/test_power.cpp (clang-cl x64, the toolchain of tools/x64-dll/build.py) into DIR and runs it:
   the opt-out is applied and reads back (process control 0x5 state 0x0, thread 0x1 0x0).
2. --server-exe EXE: runs server.exe without arguments (it prints its usage and exits 1 before reading any file) and
   requires the start-up line "power throttling opt-out (bug 0118): ... reads back control 0x5 state 0x0" on stdout.
   An unfixed server.exe has no such line: "FAIL: server.exe start-up did not apply the opt-out".
3. --rate SECONDS: the pacing loop of shell_server.exe's terrain scene in three modes (default, throttle = the state
   Windows can impose on an invisible process, optout), SECONDS each, one process per mode, interleaved twice; prints
   the per-second loop rates and per mode the median, min and max. A measurement, not a pass condition.
Exit 0 when every check passes, 1 when one fails, 2 when the build fails. Start it through quietlock.py run.
"""
import argparse
import importlib.util
import re
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LINE = re.compile(r'power throttling opt-out \(bug 0118\): .*reads back control 0x5 state 0x0')


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--server-exe', type=Path)
    ap.add_argument('--rate', type=float, default=0)
    a = ap.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m, flush=True), out / 'vcvars-env.json')
    exe = out / 'test_power.exe'
    cmd = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS', f'/Fe{exe}', f'/Fo{out}\\', str(HERE / 'test_power.cpp'),
           '/link', 'kernel32.lib', 'winmm.lib']
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(out))
    print(f'compile+link: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0 or not exe.exists():
        print('run_power_test: BUILD FAILED')
        return 2
    fails = 0
    rp = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace', creationflags=0x08000000)
    print(rp.stdout.rstrip())
    fails += rp.returncode != 0
    if a.server_exe:
        sp = subprocess.run([str(a.server_exe)], capture_output=True, text=True, errors='replace', timeout=60,
                            creationflags=0x08000000)
        text = sp.stdout + sp.stderr
        hit = [l for l in text.splitlines() if 'power throttling' in l]
        print(f'server.exe (no arguments) exit {sp.returncode}; ' + (hit[0] if hit else 'no power-throttling line'))
        if not any(LINE.search(l) for l in hit):
            print('FAIL: server.exe start-up did not apply the opt-out')
            fails += 1
        else:
            print('PASS: server.exe start-up applies the opt-out')
    if a.rate > 0:
        rates = {m: [] for m in ('default', 'throttle', 'optout', 'default-sleep', 'throttle-sleep', 'optout-sleep')}
        for rnd in (1, 2):
            for m in rates:
                rr = subprocess.run([str(exe), '--rate', m, str(a.rate)], capture_output=True, text=True,
                                    errors='replace', creationflags=0x08000000)
                vals = [int(x) for x in re.findall(r'rate [\w-]+ second \d+: (\d+) Hz', rr.stdout)]
                head = rr.stdout.splitlines()[:2]
                print(f'round {rnd} {m}: {" | ".join(head)}; rates {vals}')
                rates[m] += vals[1:]   # the first second includes start-up
        for m, v in rates.items():
            if v:
                print(f'rate {m}: n={len(v)} median {statistics.median(v):.0f} Hz, min {min(v)}, max {max(v)}')
    print(f'run_power_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
