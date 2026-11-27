"""Issue 0129: headless test of the cost of one kRtE_DbOps batch (512 writes of the DLL) on the server's database copy.

    python -I server/tests/db/run_db_write_test.py --out DIR [--bin BIN] [--deps DEPS] [--src FORK | --fork CLONE]
                                                   [--db DB] [--writes 512] [--limit 0.5] [--no-control]

Builds server/tests/db/test_db_write_cost.cpp (x64 clang-cl, the toolchain of tools/x64-dll/build.py) with the block
"0129 COPY TUNING" of server/src/remote/srv_remote_db.cpp extracted into srv_db_tune.gen.h (the server's own text),
linked against an import library of CvGameDatabase_x64.dll, and runs it beside CvGameDatabase_x64.dll and lua51_x64.dll
from BIN on two copies of a game-shaped database placed in DIR:

  arm tuned    the server's tuning applied: the batch (512 writes on the server's copy + the proxy's replay on an
               engine stand-in) must take at most --limit seconds, every result equal, both databases identical;
  arm control  no tuning (SQLite's defaults, the server before 0129): must FAIL the same limit, or the test could not
               see the bug on this disk (skipped with --no-control; it stops as soon as the limit is passed).

Inputs (issue 0152: each has a stable default, so only --out is needed):
  --bin   default: <release of deps/dll.lock>/bin, the pinned layout (COMMON 18; the rule of tools/d10/run_d10.py,
          issue 0133). A re-pin moves it.
  --deps  default: not needed. The import library is made in DIR from the export table of BIN's own
          CvGameDatabase_x64.dll (llvm-lib /def), so it names exactly the DLL the test runs with. With --deps, the
          build_all.py --out folder's stubs/CvGameDatabase_x64.lib is used instead (as before 0152).
  --src   default: the fork's include folders at deps/dll.lock's `commit`, taken with `git archive` from --fork
          (default: tools/x64-dll/build.py DEFAULT_SRC, the long-lived fork clone; read only, whatever its checkout)
          into DIR/src, kept there for the next run. With --src, that tree is used as given.
  --db    default: a database the test makes itself in DIR (make_db: tables in the form of the game database's tables
          the DLL's DatabaseRemapper updates first, `ID integer primary key autoincrement`, `Type text not null unique`
          and a few columns, page size 1024, journal DELETE, plus a filler table to the game database's size, about
          6.8 MB; nothing taken from the game's files, D83). With --db, two copies of that game database file (never
          written), e.g. a server run's <run>/db/Civ5DebugDatabase.1.db.

Put DIR on the disk you want to judge: the bug lives on a rotating disk (the workstation's D:, 0.18 s a write untuned);
on an NVMe disk the control still fails 0.5 s (about 4 ms a write untuned = 2 s a batch).
Limit: 0.5 s for 512 writes (about 1 ms a write); measured 2026-11-13 on D: tuned ~0.02-0.1 s (research:
research/03-dll-engine-boundary/proxy-design.md, "Mod activation on the proxy route", issues 0129 and 0152).
Exit 0 when the tuned arm passes and the control fails as it must; 1 otherwise; 2 on a set-up or build error.
DIR must be outside the repository; nothing is written anywhere else.
"""
import argparse
import importlib.util
import io
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
import tarfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRC_DB_CPP = REPO / 'server' / 'src' / 'remote' / 'srv_remote_db.cpp'
BLOCK = re.compile(r'^// BEGIN 0129 COPY TUNING.*?^// END 0129 COPY TUNING$', re.S | re.M)
DB_LIB = 'CvGameDatabase_x64'


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def lock_release_dir(lock):
    """The release folder a deps/*.lock names: the first absolute path in its `release` value, continuation lines
    included. None when the file or the path is missing. (The same rule as tools/d10/run_d10.py lock_release_dir.)"""
    try:
        text = Path(lock).read_text(encoding='utf-8')
    except OSError:
        return None
    m = re.search(r'^release\s*=(.*(?:\n[ \t].*)*)', text, re.M)
    p = re.search(r'[A-Za-z]:/[^\s(),]+', m.group(1)) if m else None
    return Path(p.group(0).rstrip('/')) if p else None


_X64_RELEASE = lock_release_dir(REPO / 'deps' / 'dll.lock')
DEFAULT_BIN = _X64_RELEASE / 'bin' if _X64_RELEASE else None

# make_db sizes, in the shape of the game database: 9047's server copy (pin x64-04b1211b-pa803776, CP/VP/EUI front end)
# has 830 tables, 183 indexes, page size 1024, journal DELETE, 6,817,792 B; the first tables with an integer ID by name
# are `ID integer primary key autoincrement, Type text not null unique, ...` with 0-303 rows, most 4-60
# [C, 2026-11-13, Python sqlite3 read-only on the file].
DB_ROWS_PER_TABLE = 40
DB_FILLER_BYTES = 6_500_000


def make_db(path, writes: int) -> str:
    """A database for the test made from nothing (no game data): enough `ID integer primary key autoincrement` /
    `Type text not null unique` tables for `writes` targets (the form of the tables DatabaseRemapper updates first),
    then a filler table without ID (test_db_write_cost skips it) to the game database's size. Deterministic.
    Returns a one-line summary."""
    path = Path(path)
    if path.exists():
        path.unlink()
    ntab = writes // DB_ROWS_PER_TABLE + 4
    con = sqlite3.connect(str(path))
    try:
        con.execute('PRAGMA page_size = 1024')
        con.execute('PRAGMA journal_mode = DELETE')
        for t in range(ntab):
            name = f'Synth{t:03d}'
            up = name.upper()
            con.execute(f"CREATE TABLE {name} ('ID' integer primary key autoincrement , 'Type' text not null unique , "
                        "'Description' text , 'Help' text , 'Cost' integer default 0 , 'Flag' boolean default 0 , "
                        "'Other' text )")
            con.executemany(f'INSERT INTO {name} (Type, Description, Help, Cost, Flag, Other) VALUES (?, ?, ?, ?, ?, ?)',
                            [(f'{up}_{i:04d}', f'TXT_KEY_{up}_{i:04d}', f'TXT_KEY_{up}_{i:04d}_HELP', (i * 37) % 500,
                              i % 2, None) for i in range(DB_ROWS_PER_TABLE)])
        con.execute("CREATE TABLE ZFiller ('Name' text not null , 'Value' text )")
        width = 200
        con.executemany('INSERT INTO ZFiller (Name, Value) VALUES (?, ?)',
                        [(f'FILLER_{i:06d}', f'{i:08d}' * (width // 8)) for i in range(DB_FILLER_BYTES // (width + 20))])
        con.commit()
    finally:
        con.close()
    return (f'made by the test: {ntab} tables x {DB_ROWS_PER_TABLE} rows with an integer ID + filler, page size 1024, '
            f'journal DELETE, {path.stat().st_size:,} B')


def pe_exports(dll):
    """(DLL name, [(export name, is_data)]) from a PE32+ file's export table. is_data: the address lies in a section
    that is not executable (a DATA export in the .def)."""
    b = Path(dll).read_bytes()
    pe = struct.unpack_from('<I', b, 0x3C)[0]
    if b[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'{dll}: not a PE file')
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    opt_size = struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', b, opt)[0] != 0x20B:
        raise ValueError(f'{dll}: not PE32+ (x64)')
    exp_rva, exp_size = struct.unpack_from('<II', b, opt + 112)
    secs = []
    for i in range(nsec):
        o = opt + opt_size + 40 * i
        vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', b, o + 8)
        secs.append((va, max(vsize, rawsize), rawptr, struct.unpack_from('<I', b, o + 36)[0]))

    def sec(rva):
        for s in secs:
            if s[0] <= rva < s[0] + s[1]:
                return s
        raise ValueError(f'{dll}: RVA {rva:#x} in no section')

    def off(rva):
        s = sec(rva)
        return rva - s[0] + s[2]

    def cstr(rva):
        o = off(rva)
        return b[o:b.index(b'\0', o)].decode('ascii')

    e = off(exp_rva)
    name_rva, _base, _nfun, nnames, afun, anames, aords = struct.unpack_from('<IIIIIII', b, e + 12)
    out = []
    for i in range(nnames):
        nm = cstr(struct.unpack_from('<I', b, off(anames) + 4 * i)[0])
        ordi = struct.unpack_from('<H', b, off(aords) + 2 * i)[0]
        frva = struct.unpack_from('<I', b, off(afun) + 4 * ordi)[0]
        forwarder = exp_rva <= frva < exp_rva + exp_size
        out.append((nm, not forwarder and not (sec(frva)[3] & 0x20000000)))  # IMAGE_SCN_MEM_EXECUTE
    return cstr(name_rva), out


def make_import_lib(dll: Path, lib: Path, llvm_bin: Path) -> str:
    """lib = the import library of dll, from its export table (llvm-lib /def). Returns a summary; raises on failure."""
    name, exports = pe_exports(dll)
    deff = lib.with_suffix('.def')
    deff.write_text(f'LIBRARY "{name}"\nEXPORTS\n' + ''.join(f'  "{n}"{" DATA" if d else ""}\n' for n, d in exports),
                    encoding='ascii')
    cp = subprocess.run([str(llvm_bin / 'llvm-lib.exe'), '/nologo', '/machine:x64', f'/def:{deff}', f'/out:{lib}'],
                        capture_output=True, text=True, errors='replace')
    if cp.returncode != 0 or not lib.exists():
        raise RuntimeError(f'llvm-lib rc={cp.returncode}: {(cp.stdout + cp.stderr).strip()[:400]}')
    return f'{lib} from the export table of {dll} ({len(exports)} exports, {sum(d for _, d in exports)} of them data)'


def extract_src(fork: Path, commit: str, dirs, dest: Path) -> str:
    """dest = the folders `dirs` of the fork at `commit`, by `git archive` (read only on the clone; its checkout and
    branch do not matter). Kept and reused while dest/.commit names the same commit. Returns a summary; raises."""
    stamp = dest / '.commit'
    if stamp.exists() and stamp.read_text(encoding='ascii').strip() == commit:
        return f'{dest} (git archive of {commit[:9]} from {fork}, kept from an earlier run)'
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    cp = subprocess.run(['git', '-C', str(fork), 'archive', '--format=tar', commit] + list(dirs), capture_output=True)
    if cp.returncode != 0:
        raise RuntimeError(f'git archive of {commit[:9]} in {fork} failed: '
                           f'{cp.stderr.decode(errors="replace").strip()[:300]}')
    with tarfile.open(fileobj=io.BytesIO(cp.stdout)) as tf:
        tf.extractall(dest, filter='data')
    stamp.write_text(commit + '\n', encoding='ascii')
    return f'{dest} (git archive of {commit[:9]} from {fork}: {", ".join(dirs)})'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True, help='build and data folder (outside the repository)')
    ap.add_argument('--bin', type=Path, default=DEFAULT_BIN,
                    help='bin folder of the pinned layout: CvGameDatabase_x64.dll, lua51_x64.dll (default: <release of '
                         'deps/dll.lock>/bin = %(default)s)')
    ap.add_argument('--deps', type=Path, help='build_all.py --out folder of the same build (stubs/*.lib); default: an '
                                              "import library made from BIN's CvGameDatabase_x64.dll")
    ap.add_argument('--src', type=Path, help='DLL fork source at the pin (CvGameDatabase/include); default: its include '
                                             "folders taken from --fork at deps/dll.lock's commit")
    ap.add_argument('--fork', type=Path, help='fork clone that holds the locked commit (read only; default: '
                                              'tools/x64-dll/build.py DEFAULT_SRC)')
    ap.add_argument('--db', type=Path, help='a game database file (copied; never written); default: one the test makes')
    ap.add_argument('--writes', type=int, default=512)
    ap.add_argument('--limit', type=float, default=0.5, help='seconds for the whole batch (default %(default)s)')
    ap.add_argument('--no-control', action='store_true', help='skip the untuned arm')
    ap.add_argument('--quietlock-override', metavar='"WHO: WHY"', help='COMMON rule 16 (emergencies only)')
    a = ap.parse_args()
    if a.bin is None:
        ap.error('deps/dll.lock names no release folder: pass --bin')
    if a.src and a.fork:
        ap.error('--fork only says where to take the source from when --src is not given')

    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate as _launch_gate
        _launch_gate.require('the database write-cost test (server/tests/db/run_db_write_test.py)', a.quietlock_override)
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; test goes on', file=sys.stderr)

    out = a.out.resolve()
    try:
        out.relative_to(REPO)
        print('run_db_write_test: --out must be outside the repository')
        return 2
    except ValueError:
        pass
    build = out / 'build'
    data = out / 'data'
    for p in (build, data):
        if p.exists():
            shutil.rmtree(p)
        p.mkdir(parents=True)

    m = BLOCK.search(SRC_DB_CPP.read_text(encoding='utf-8'))
    if not m:
        print(f'run_db_write_test: no "0129 COPY TUNING" block in {SRC_DB_CPP}')
        return 2
    (build / 'srv_db_tune.gen.h').write_text(
        '// Generated by server/tests/db/run_db_write_test.py from server/src/remote/srv_remote_db.cpp. Do not edit.\n'
        '#pragma once\n#include <stdio.h>\n#include <string.h>\n#include <string>\n#include "CvGameDatabase.h"\n'
        + m.group(0) + '\n', encoding='utf-8')
    for f in (DB_LIB + '.dll', 'lua51_x64.dll'):
        if not (a.bin / f).exists():
            print(f'run_db_write_test: {a.bin / f} not found')
            return 2
        shutil.copy2(a.bin / f, build / f)

    x64build = load_x64build()
    try:
        if a.deps:
            lib = a.deps / 'stubs' / (DB_LIB + '.lib')
            lib_how = f'{lib} (--deps)'
        else:
            lib = build / (DB_LIB + '.lib')
            lib_how = make_import_lib(a.bin / (DB_LIB + '.dll'), lib, x64build.LLVM_BIN)
        if a.src:
            src, src_how = a.src, f'{a.src} (--src)'
        else:
            commit = x64build.read_dll_lock()['commit']
            fork = a.fork or x64build.DEFAULT_SRC
            dirs = [d.replace('\\', '/') for d in x64build.INCLUDE_DIRS if d != 'CvGameCoreDLL_Expansion2']
            src = out / 'src'
            src_how = extract_src(fork, commit, dirs, src)
    except (OSError, ValueError, RuntimeError, KeyError) as e:
        print(f'run_db_write_test: set-up: {e}' + ('' if a.src else ' (pass --src, or --fork with a clone that holds '
                                                                     "deps/dll.lock's commit)"))
        return 2
    inc = src / 'CvGameDatabase' / 'include'
    if not lib.exists() or not inc.exists():
        print(f'run_db_write_test: {lib} or {inc} not found')
        return 2
    print(f'bin {a.bin}{" (deps/dll.lock release)" if a.bin == DEFAULT_BIN else ""}')
    print(f'import library {lib_how}')
    print(f'source {src_how}')

    env = x64build.load_vcvars_env(lambda msg, echo=True: print(msg, flush=True), out / 'vcvars-env.json')
    exe = build / 'test_db_write_cost.exe'
    # the server's predefines and fork include folders (server/build_server.py), so the database headers compile as there
    defs = [f'/D{d}' for d in x64build.PREDEFS['debug'] if d not in ('_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS', 'VPDEBUG')]
    incs = [f'/I{src / d}' for d in x64build.INCLUDE_DIRS]
    cmd = [str(x64build.CLANG_CL), '-m64', '-msse3', '/nologo', '/MD', '/EHsc', '/std:c++14', '/O2', '-fuse-ld=lld',
           '/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING', '/D_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS', '-Wno-everything'] + defs + \
          [f'/I{build}', f'/I{inc}', f'/I{REPO / "proxy" / "include"}'] + incs + [
           f'/Fe{exe}', f'/Fo{build}\\', str(HERE / 'test_db_write_cost.cpp'), '/link', str(lib), 'kernel32.lib']
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(build))
    print(f'compile+link: rc={cp.returncode}')
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0 or not exe.exists():
        print('run_db_write_test: BUILD FAILED')
        return 2

    if a.db:
        db, db_how = a.db, f'{a.db} (--db)'
    else:
        db = data / 'made.db'
        db_how = f'{db} ({make_db(db, a.writes)})'

    def arm(name, mode):
        d = data / name
        d.mkdir()
        srv, eng = d / 'server-copy.db', d / 'engine-standin.db'
        shutil.copyfile(db, srv)
        shutil.copyfile(db, eng)
        t0 = time.time()
        rp = subprocess.run([str(exe), str(srv), str(eng), str(a.writes), str(a.limit), mode], capture_output=True,
                            text=True, errors='replace', cwd=str(build))
        print(f'--- arm {name} ({mode}), exit {rp.returncode}, {time.time() - t0:.1f} s wall')
        print((rp.stdout + rp.stderr).rstrip())
        return rp.returncode

    print(f'database {db_how}; data folder {data}; {a.writes} writes; limit {a.limit} s')
    rc_t = arm('tuned', 'tuned')
    ok = rc_t == 0
    if not a.no_control:
        rc_c = arm('control', 'untuned')
        if rc_c != 1:
            print('run_db_write_test: the control (no tuning) did not fail: this disk cannot show the bug, or the limit '
                  'is too loose')
            ok = False
    shutil.rmtree(data, ignore_errors=True)
    print(f'run_db_write_test: {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
