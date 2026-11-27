"""Issue 4253 (epic 0334): the server's run-time major count, the boot guard on the player totals, and the poisoned
split names in server and proxy code.

    python -I server/tests/limits/run_player_limits_test.py --out DIR [--bin BIN] [--src FORK | --fork CLONE] [--asan]

1. Run-time read and guard. Builds server/tests/limits/test_player_limits.cpp (x64 clang-cl, the server's toolchain)
   with the block "4253 PLAYER LIMITS" of server/src/srv_main.cpp extracted into srv_limits.gen.h (the server's own
   text), linked against an import library of BIN's CvGameDatabase_x64.dll, and runs it on databases the test makes in
   DIR (a `Defines` table in the game database's form; nothing taken from the game's files, D83). Every case checks the
   exit code and the output line: MAX_MAJOR_CIVS 22, 43, 2 and 62 pass with that count; a missing row, 0, 1, 63, 70, a
   text and a NULL value fail and name the value; MAX_PLAYERS 80 or MAX_TEAMS 80 (a DLL with other totals) fail;
   MAX_MAJOR_CIVS + MAX_MINOR_CIVS != MAX_CIV_PLAYERS fails; a database without `Defines` fails.
2. Poisoning. Probe translation units are compiled (syntax only) as the server compiles its own (x64, the fork's include
   folders, server/src/srv_common.h) and as the proxy compiles its own (x86 in the VS 2008 environment,
   proxy/src/vp_gen_common.h -> proxy_rt.h): a probe that uses only the totals must compile (the control: the include
   set-up works); a probe that uses MAX_MAJOR_CIVS, MAX_MINOR_CIVS, OBSERVER_TEAM or MAX_PREGAME_MAJOR_CIVS, in code or
   in `#if`, must fail with the poison's message.
3. --asan: the program of part 1 built with MSVC cl /fsanitize=address (x64) after a canary (one byte past a heap block)
   is caught; every case of part 1 again, with no AddressSanitizer report. Runs under shell/tools/memcap.py (2048 MB).

Inputs:
  --bin   default: <release of deps/dll.lock>/bin (the pinned layout; CvGameDatabase_x64.dll, lua51_x64.dll).
  --src   default: the fork's include folders at deps/dll.lock's `commit`, taken with `git archive` from --fork (default
          tools/x64-dll/build.py DEFAULT_SRC; read only) into DIR/src, kept for the next run.
Exit 0 when every check passes, 1 when one fails, 2 on a set-up or build error. DIR must be outside the repository.
Research: research/03-dll-engine-boundary/player-limits.md section 8.2.
"""
import argparse
import importlib.util
import re
import shutil
import sqlite3
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SRV_MAIN = REPO / 'server' / 'src' / 'srv_main.cpp'
BLOCK = re.compile(r'^// BEGIN 4253 PLAYER LIMITS.*?^// END 4253 PLAYER LIMITS$', re.S | re.M)
DB_LIB = 'CvGameDatabase_x64'
NOWIN = 0x08000000  # CREATE_NO_WINDOW
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# the database test's helpers (issue 0129/0152): lock reading, import library from a DLL's export table, git archive
dbt = _load('run_db_write_test', REPO / 'server' / 'tests' / 'db' / 'run_db_write_test.py')

# ---- the databases ------------------------------------------------------------------------------------------------
# The DLL's rows (CvDllDatabaseUtility.cpp:265-274 at 04b1211b8, InsertGameDefine) for the 22-major build.
BASE = {'MAX_CIV_PLAYERS': 63, 'MAX_CIV_TEAMS': 63, 'MAX_MAJOR_CIVS': 22, 'MAX_MINOR_CIVS': 41, 'MAX_PLAYERS': 64,
        'MAX_TEAMS': 64, 'BARBARIAN_PLAYER': 63, 'BARBARIAN_TEAM': 63}


def split(majors, **extra):
    d = dict(BASE, MAX_MAJOR_CIVS=majors, MAX_MINOR_CIVS=63 - majors)
    d.update(extra)
    return d


MISSING = object()
# (name, rows (None: no Defines table), expected exit, expected output fragment)
CASES = [
    ('major22', split(22), 0, 'RESULT PASS majors 22 minors 41 players 64 teams 64 civ_players 63'),
    ('major43', split(43), 0, 'RESULT PASS majors 43 minors 20 players 64 teams 64 civ_players 63'),
    ('major2', split(2), 0, 'RESULT PASS majors 2 minors 61'),
    ('major62', split(62), 0, 'RESULT PASS majors 62 minors 1'),
    ('major_missing', split(22, MAX_MAJOR_CIVS=MISSING), 1,
     "RESULT FAIL the game database's Defines has no row MAX_MAJOR_CIVS"),
    ('major0', split(0), 1, "RESULT FAIL the DLL's MAX_MAJOR_CIVS is 0, outside 2..62"),
    ('major1', split(1), 1, "RESULT FAIL the DLL's MAX_MAJOR_CIVS is 1, outside 2..62"),
    ('major63', split(63), 1, "RESULT FAIL the DLL's MAX_MAJOR_CIVS is 63, outside 2..62"),
    ('major70', split(70), 1, "RESULT FAIL the DLL's MAX_MAJOR_CIVS is 70, outside 2..62"),
    ('major_text', split(22, MAX_MAJOR_CIVS='forty-three'), 1,
     "RESULT FAIL the game database's Defines row MAX_MAJOR_CIVS is not an integer ('forty-three')"),
    ('major_null', split(22, MAX_MAJOR_CIVS=None), 1,
     "RESULT FAIL the game database's Defines row MAX_MAJOR_CIVS is not an integer (NULL)"),
    ('players80', split(22, MAX_PLAYERS=80), 1,
     "RESULT FAIL the DLL's MAX_PLAYERS is 80, this server is built for 64: this server.exe cannot run this DLL"),
    ('teams80', split(22, MAX_TEAMS=80), 1, "RESULT FAIL the DLL's MAX_TEAMS is 80, this server is built for 64"),
    ('civplayers79', split(22, MAX_CIV_PLAYERS=79), 1, "RESULT FAIL the DLL's MAX_CIV_PLAYERS is 79, this server is built for 63"),
    ('split_mismatch', split(43, MAX_MINOR_CIVS=41), 1,
     "RESULT FAIL the DLL's MAX_MAJOR_CIVS 43 + MAX_MINOR_CIVS 41 is not MAX_CIV_PLAYERS 63"),
    ('no_defines', None, 1, "RESULT FAIL cannot query the game database's Defines for MAX_PLAYERS"),
]


def make_db(path, rows):
    """A database with the game database's `Defines` table (Name text primary key, Value integer) holding `rows`, a few
    other defines, and an unrelated table; rows=None: no Defines table at all."""
    if path.exists():
        path.unlink()
    con = sqlite3.connect(str(path))
    try:
        con.execute("CREATE TABLE Civilizations ('ID' integer primary key autoincrement, 'Type' text not null unique)")
        con.execute("INSERT INTO Civilizations (Type) VALUES ('CIVILIZATION_TEST')")
        if rows is not None:
            con.execute("CREATE TABLE Defines ('Name' text primary key, 'Value' integer)")
            con.execute("INSERT INTO Defines (Name, Value) VALUES ('START_YEAR', -4000), ('MAX_CITY_RADIUS', 5)")
            con.executemany('INSERT INTO Defines (Name, Value) VALUES (?, ?)',
                            [(k, v) for k, v in rows.items() if v is not MISSING])
        con.commit()
    finally:
        con.close()


# ---- helpers ------------------------------------------------------------------------------------------------------
def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)


def checker():
    res = {'fail': 0, 'n': 0}

    def check(name, ok, detail=''):
        res['n'] += 1
        res['fail'] += not ok
        print(f'{"PASS" if ok else "FAIL"}  {name}' + (f'\n      {detail}' if detail and not ok else ''))
        return ok
    return check, res


def run_cases(check, exe, data, label, env=None, memcap=None):
    for name, rows, want_rc, want_out in CASES:
        db = data / f'{name}.db'
        make_db(db, rows)
        cp = run((memcap or []) + [str(exe), str(db)], cwd=str(exe.parent), env=env)
        out = (cp.stdout + cp.stderr).strip()
        ok = cp.returncode == want_rc and want_out in out and 'AddressSanitizer' not in out
        check(f'{label} {name}: exit {cp.returncode} (want {want_rc}), output names it', ok,
              f'output: {out[-1500:]}\n      want: {want_out}')


# ---- part 2: the probes -------------------------------------------------------------------------------------------
TOTALS_CODE = 'int f() { return MAX_PLAYERS + MAX_TEAMS + MAX_CIV_PLAYERS + (int)BARBARIAN_PLAYER; }\n'
POISONED = [
    ('major_code', 'int f() { int n = 0; for(int i = 0; i < MAX_MAJOR_CIVS; ++i) n += i; return n; }\n', 'MAX_MAJOR_CIVS'),
    ('minor_code', 'int f() { return MAX_MINOR_CIVS; }\n', 'MAX_MINOR_CIVS'),
    ('observer_code', 'int f() { return (int)OBSERVER_TEAM; }\n', 'OBSERVER_TEAM'),
    ('pregame_code', 'int f() { return MAX_PREGAME_MAJOR_CIVS; }\n', 'MAX_PREGAME_MAJOR_CIVS'),
    ('major_if', '#if MAX_MAJOR_CIVS > 22\nint g_big = 1;\n#endif\nint f() { return 0; }\n', 'MAX_MAJOR_CIVS'),
]
# the poison's message as each header words it (srv_common.h, proxy_rt.h); MAX_PREGAME_MAJOR_CIVS has its own
MESSAGE = {'server': 'read at run time in the server', 'proxy': 'differs between the DLL builds'}
MESSAGE_PREGAME = 'exists only in the 43-major DLL build'


def probes(check, side, include, compile_one, work):
    work.mkdir(parents=True, exist_ok=True)
    ctl = work / f'{side}_totals.cpp'
    ctl.write_text(f'#include "{include}"\n' + TOTALS_CODE, encoding='ascii')
    cp = compile_one(ctl)
    out = (cp.stdout + cp.stderr).strip()
    if not check(f'{side} probe totals (control): compiles (exit {cp.returncode})', cp.returncode == 0, out[-3000:]):
        return
    for name, code, macro in POISONED:
        p = work / f'{side}_{name}.cpp'
        p.write_text(f'#include "{include}"\n' + code, encoding='ascii')
        cp = compile_one(p)
        out = (cp.stdout + cp.stderr).strip()
        msg = MESSAGE_PREGAME if macro == 'MAX_PREGAME_MAJOR_CIVS' else \
            ('depends on' if macro == 'OBSERVER_TEAM' else MESSAGE[side])
        ok = cp.returncode != 0 and 'error:' in out and macro in out and msg in out and 'issue 4253' in out
        check(f'{side} probe {name}: does not compile (exit {cp.returncode}) and names {macro}', ok, out[-1500:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True, help='build and data folder (outside the repository)')
    ap.add_argument('--bin', type=Path, default=dbt.DEFAULT_BIN,
                    help='bin folder with CvGameDatabase_x64.dll and lua51_x64.dll (default %(default)s)')
    ap.add_argument('--src', type=Path, help='DLL fork source at the pin (default: taken from --fork)')
    ap.add_argument('--fork', type=Path, help='fork clone holding the locked commit (read only)')
    ap.add_argument('--asan', action='store_true', help='part 3: MSVC /fsanitize=address')
    ap.add_argument('--quietlock-override', metavar='"WHO: WHY"', help='COMMON rule 16 (emergencies only)')
    a = ap.parse_args()
    if a.bin is None:
        ap.error('deps/dll.lock names no release folder: pass --bin')
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the player-limits test (server/tests/limits/run_player_limits_test.py)', a.quietlock_override)
    except ImportError:
        print('run_player_limits_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    out = a.out.resolve()
    try:
        out.relative_to(REPO)
        print('run_player_limits_test: --out must be outside the repository')
        return 2
    except ValueError:
        pass
    build, data = out / 'build', out / 'data'
    for p in (build, data):
        if p.exists():
            shutil.rmtree(p)
        p.mkdir(parents=True)

    m = BLOCK.search(SRV_MAIN.read_text(encoding='utf-8'))
    if not m:
        print(f'run_player_limits_test: no "4253 PLAYER LIMITS" block in {SRV_MAIN}')
        return 2
    (build / 'srv_limits.gen.h').write_text(
        '// Generated by server/tests/limits/run_player_limits_test.py from server/src/srv_main.cpp. Do not edit.\n'
        '#pragma once\n#include <stdio.h>\n#include <string>\n#include "CvGameDatabase.h"\n' + m.group(0) + '\n',
        encoding='utf-8')
    for f in (DB_LIB + '.dll', 'lua51_x64.dll'):
        if not (a.bin / f).exists():
            print(f'run_player_limits_test: {a.bin / f} not found')
            return 2
        shutil.copy2(a.bin / f, build / f)

    x64build = dbt.load_x64build()
    try:
        lib = build / (DB_LIB + '.lib')
        lib_how = dbt.make_import_lib(a.bin / (DB_LIB + '.dll'), lib, x64build.LLVM_BIN)
        if a.src:
            src, src_how = a.src.resolve(), f'{a.src} (--src)'
        else:
            commit = x64build.read_dll_lock()['commit']
            fork = a.fork or x64build.DEFAULT_SRC
            dirs = [d.replace('\\', '/') for d in x64build.INCLUDE_DIRS]
            src = out / 'src'
            src_how = dbt.extract_src(fork, commit, dirs, src)
    except (OSError, ValueError, RuntimeError, KeyError) as e:
        print(f'run_player_limits_test: set-up: {e}')
        return 2
    print(f'bin {a.bin}\nimport library {lib_how}\nsource {src_how}')
    env = x64build.load_vcvars_env(lambda msg, echo=True: None, out / 'vcvars-env.json')
    check, res = checker()

    # 1. the run-time read and the guard (the server's toolchain and defines, server/build_server.py)
    defs = [f'/D{d}' for d in x64build.PREDEFS['debug'] if d not in ('_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS', 'VPDEBUG')]
    defs += ['/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING', '/D_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS']
    # the fork's headers as external headers (warnings off there), as server/build_server.py compiles them
    incs = [f'/I{build}', '/external:W0', f'/external:I{src / "CvGameDatabase" / "include"}',
            f'/external:I{src / "CvGameCoreDLLUtil" / "include"}']
    exe = build / 'test_player_limits.exe'
    cmd = [str(x64build.CLANG_CL), '-m64', '-msse3', '/nologo', '/MD', '/EHsc', '/std:c++14', '/O2', '/W3', '/WX',
           '-fuse-ld=lld', '-Wno-microsoft-include', '-Wno-pragma-pack'] + defs + incs + \
          [f'/Fe{exe}', f'/Fo{build}\\', str(HERE / 'test_player_limits.cpp'), '/link', str(lib), 'kernel32.lib']
    cp = run(cmd, env=env, cwd=str(build))
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-4000:])
    if cp.returncode != 0 or not exe.exists():
        print(f'run_player_limits_test: BUILD FAILED (clang-cl rc {cp.returncode})')
        return 2
    print(f'built {exe}')
    run_cases(check, exe, data, 'read+guard')

    # 2. the poisoning: server side (x64, srv_common.h) and proxy side (x86, VS 2008 environment, vp_gen_common.h)
    sdefs = [f'/D{d}' for d in x64build.PREDEFS['debug'] if d not in ('_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS', 'VPDEBUG')]
    sargs = [str(x64build.CLANG_CL), '-m64', '-msse3', '/nologo', '/Zs', '/MD', '/EHsc', '/std:c++14', '/W3',
             '/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING', '-Wno-everything'] + sdefs + \
            ['/external:W0'] + [f'/external:I{src / d}' for d in x64build.INCLUDE_DIRS] + \
            [f'/I{REPO / "server" / "src"}']
    probes(check, 'server', 'srv_common.h', lambda p: run(sargs + [str(p)], env=env, cwd=str(p.parent)),
           build / 'probes')
    tc = _load('proxy_toolchain', REPO / 'proxy' / 'gen' / 'toolchain.py')
    pargs = tc.ARCH_ARGS['x86'] + ['/nologo', '/Zs', '/MD', '/EHsc', '/Zc:wchar_t', '-msse3', '-Wno-everything'] + \
        ['/D' + d for d in tc.DEFINES] + ['/I' + str(src / i) for i in tc.INCLUDES] + \
        ['/I' + str(REPO / 'proxy' / 'src'), '/I' + str(REPO / 'proxy' / 'include'), '/I' + str(REPO / 'ipc' / 'include')]
    probes(check, 'proxy', 'vp_gen_common.h', lambda p: tc.run_in_env('x86', [tc.CLANG] + pargs + [str(p)],
                                                                         cwd=str(p.parent)), build / 'probes')

    # 3. --asan: MSVC cl /fsanitize=address (x64)
    if a.asan:
        cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
        if not cl:
            print('run_player_limits_test: ASan: cl.exe not found in the vcvars x64 PATH')
            return 2
        (build / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
        aflags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
                  '/D_CRT_SECURE_NO_WARNINGS'] + [d for d in defs] + incs
        aexe = build / 'test_player_limits_asan.exe'
        for name, srcs, fl, extra in (('asan_canary.exe', [build / 'asan_canary.cpp'],
                                       ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                      (aexe.name, [HERE / 'test_player_limits.cpp'], aflags, ['/link', str(lib)])):
            cp = run([cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(build / name), '/Fo' + str(build) + '\\',
                                                           '/Fd' + str(build / (name + '.pdb'))] + extra,
                     cwd=str(build), env=env)
            print(f'asan compile {name}: rc={cp.returncode}')
            if cp.returncode:
                print((cp.stdout + cp.stderr)[-4000:])
                return 2
        memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
        cp = run(memcap + [str(build / 'asan_canary.exe')], cwd=str(build), env=env)
        text = cp.stdout + cp.stderr
        if not check(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow)',
                     cp.returncode != 0 and 'heap-buffer-overflow' in text, text[-2000:]):
            print('run_player_limits_test: ASan not active; the ASan cases are not run')
        else:
            run_cases(check, aexe, data, 'asan', env=env, memcap=memcap)

    shutil.rmtree(data, ignore_errors=True)
    print(f'run_player_limits_test: {"PASS" if not res["fail"] else "FAIL"} ({res["n"]} checks, {res["fail"]} failed)')
    return 1 if res['fail'] else 0


if __name__ == '__main__':
    sys.exit(main())
