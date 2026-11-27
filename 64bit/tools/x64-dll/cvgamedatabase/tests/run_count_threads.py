"""Two-thread test of Database::Connection::Count (issue 0165): build count_threads.cpp against a built
CvGameDatabase library and run it for some rounds.

    python -I tools/x64-dll/cvgamedatabase/tests/run_count_threads.py --lib-dir DIR --src FORK --out DIR
        [--arch x64] [--config release] [--threads 4] [--ms 2000] [--rounds 5] [--expect pass|fail]

--lib-dir holds CvGameDatabase_<arch>.lib and .dll (made by build_db.py --out DIR). --src is the fork (headers),
checked against deps/dll.lock as every --src here. Each round runs count_threads.exe on a fresh scratch database in
--out; a round passes when the program exits 0 (every Count returned the row count). A crash (0xC0000005, the dump of
queue ticket 0153) or a wrong count fails the round.

--expect pass (default): exit 0 when every round passed, 1 otherwise. --expect fail (the negative check on a library
built from the code before the fix): exit 0 when at least one round failed, 1 when all passed. Exit 2: build or usage
error. The summary line "run_count_threads: RESULT ..." names the rounds and the exit code of each.
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DBDIR = HERE.parent
sys.path.insert(0, str(DBDIR))
import build_db  # noqa: E402  (clang-cl, lld-link, vcvars, the fork's include dirs and predefines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lib-dir', type=Path, required=True)
    ap.add_argument('--src', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--arch', choices=['x64', 'x86'], default='x64')
    ap.add_argument('--config', choices=['debug', 'release'], default='release')
    ap.add_argument('--threads', type=int, default=4)
    ap.add_argument('--ms', type=int, default=2000)
    ap.add_argument('--rounds', type=int, default=5)
    ap.add_argument('--expect', choices=['pass', 'fail'], default='pass')
    ap.add_argument('--unlocked', action='store_true')
    a = ap.parse_args()
    if a.rounds < 1 or a.threads < 1 or a.ms < 1:
        print('run_count_threads: rounds, threads and ms must be positive', file=sys.stderr)
        return 2
    try:
        src = Path(build_db.x64build.resolve_src(a.src, a.unlocked)['src'])
    except build_db.x64build.SrcError as e:
        print(f'run_count_threads: FAILED: {e}', file=sys.stderr)
        return 2
    lib = a.lib_dir / f'CvGameDatabase_{a.arch}.lib'
    dll = a.lib_dir / f'CvGameDatabase_{a.arch}.dll'
    for p in (lib, dll):
        if not p.is_file():
            print(f'run_count_threads: missing {p}', file=sys.stderr)
            return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = build_db.vcvars_env(a.arch)
    obj = out / f'count_threads-{a.arch}.obj'
    exe = out / 'count_threads.exe'
    args = build_db.base_args(a.arch, a.config) + ['/std:c++14', '/W4', '/WX']
    args += [f'/D{d}' for d in build_db.PREDEFS[a.config]]
    args += [f'/imsvc{src / d}' for d in build_db.INCLUDE_DIRS]
    r = subprocess.run([str(build_db.CLANG_CL)] + args + [str(HERE / 'count_threads.cpp'), f'/Fo{obj}'],
                       env=env, capture_output=True, text=True)
    if r.returncode != 0:
        print((r.stdout + r.stderr).strip(), file=sys.stderr)
        return 2
    r = subprocess.run([str(build_db.LLD_LINK), '/nologo', f'/machine:{a.arch}', f'/out:{exe}', '/DEBUG',
                        f'/pdb:{exe.with_suffix(".pdb")}', str(obj), str(lib)], env=env, capture_output=True, text=True)
    if r.returncode != 0:
        print((r.stdout + r.stderr).strip(), file=sys.stderr)
        return 2
    shutil.copy2(dll, out / dll.name)
    print(f'run_count_threads: {exe} linked against {lib} ({a.arch} {a.config}); {a.rounds} rounds x {a.threads} '
          f'threads x {a.ms} ms, expect {a.expect}', flush=True)
    codes = []
    for i in range(a.rounds):
        t0 = time.time()
        try:
            r = subprocess.run([str(exe), str(out / f'scratch-{i}.db'), str(a.threads), str(a.ms)], cwd=out,
                               capture_output=True, text=True, timeout=a.ms / 1000 + 60)
            rc = r.returncode & 0xFFFFFFFF
            text = (r.stdout + r.stderr).strip()
        except subprocess.TimeoutExpired:
            rc, text = -1, 'timeout'
        codes.append(rc)
        print(f'round {i + 1}: exit 0x{rc:X} in {time.time() - t0:.1f} s: {text or "(no output)"}', flush=True)
    failed = sum(1 for c in codes if c != 0)
    ok = (failed == 0) if a.expect == 'pass' else (failed > 0)
    rc = 0 if ok else 1
    print(f'run_count_threads: RESULT {"PASS" if ok else "FAIL"} (expect {a.expect}): {failed} of {len(codes)} '
          f'rounds failed; exit codes {", ".join(f"0x{c:X}" for c in codes)}; exit {rc}', flush=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
