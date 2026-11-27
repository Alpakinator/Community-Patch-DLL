"""Build the CvLocalization replacement (issue 1701): a shared DLL plus import library.

    python build_loc.py --out DIR --db-dir DIR [--arch x64|x86] [--config debug|release] [--src FORK] [--unlocked]

`--db-dir` holds CvGameDatabase_<arch>.lib (tools/x64-dll/cvgamedatabase/build_db.py, same --arch and --config): the
dictionary runs its SQL through our Database::Connection/Results. `--src` (the fork's headers) is checked against
deps/dll.lock by tools/x64-dll/build.py's resolve_src, like every script here (issue 1508); `--unlocked` accepts
another git commit on purpose.

Writes DIR/CvLocalization_<arch>.dll, .lib (import library), .pdb, .map, .exports.txt and DIR/build_loc_<arch>.log.
Objects go to DIR/obj-loc-<arch>-<config>/.

The classes are exported with `CvLocalizationAPI = __declspec(dllexport)`, so the export names are exactly the ones
the game DLL's `__imp_` references (x64) and the closed CvLocalizationWin32.lib (x86) carry. tools/x64-dll/build.py
--stubs DIR links every *.lib in DIR, so `--out <stubs dir>` makes the game DLL pick this import library up.

Toolchain and defines as cvgamedatabase/build_db.py (clang-cl 19.1.7, MSVC STL/CRT via vcvars, /std:c++14, /MD, the
game DLL's per-config defines: VPDEBUG changes layouts). Our C++ compiles with /W4 /WX; the fork's headers are
system includes. No third-party code: no ICU (see README.md for how number formatting, spellout and case mapping are
done without it).
"""
import argparse
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'cvgamedatabase'))
import build_db  # noqa: E402  toolchain, vcvars, defines, resolve_src (deps/dll.lock check)

x64build = build_db.x64build
CLANG_CL = build_db.CLANG_CL
LLD_LINK = build_db.LLD_LINK
LLVM_BIN = build_db.LLVM_BIN
PREDEFS = build_db.PREDEFS
DEFAULT_SRC = build_db.DEFAULT_SRC
INCLUDE_DIRS = [r'CvLocalization\include', r'CvGameDatabase\include']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--db-dir', type=Path, required=True, help='directory with CvGameDatabase_<arch>.lib')
    ap.add_argument('--arch', choices=['x64', 'x86'], default='x64')
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source (headers): git checkout at the deps/dll.lock commit or a plain copy of it; '
                         f'default {DEFAULT_SRC} if it is at that commit')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit (fork development)')
    a = ap.parse_args()
    try:
        a.src = x64build.resolve_src(a.src, a.unlocked)['src']
    except x64build.SrcError as e:
        sys.exit(f'build_loc: FAILED: {e}')

    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = build_db.Log(out / f'build_loc_{a.arch}.log')
    t0 = time.time()
    dblib = a.db_dir.resolve() / f'CvGameDatabase_{a.arch}.lib'
    if not dblib.is_file():
        log(f'error: {dblib} not found (build it with cvgamedatabase/build_db.py --arch {a.arch})')
        sys.exit(2)
    log(f'build_loc: arch={a.arch} config={a.config} src={a.src} out={out} db={dblib}')
    env = x64build.repro_env(build_db.vcvars_env(a.arch))   # issue 0115
    build_db.REPRO.update(out=out, src=Path(a.src))
    ver = subprocess.run([str(CLANG_CL), '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    log(f'clang-cl: {ver}; MSVC {env.get("VCToolsVersion", "?")}; '
        f'SDK {env.get("WindowsSDKVersion", "?").strip(chr(92))}')

    od = out / f'obj-loc-{a.arch}-{a.config}'
    od.mkdir(parents=True, exist_ok=True)
    cpp = build_db.base_args(a.arch, a.config) + ['/std:c++14', '/W4', '/WX',
                                                   '/DCvLocalizationAPI=__declspec(dllexport)']
    cpp += [f'/D{d}' for d in PREDEFS[a.config]]
    # The fork's headers are system includes: /W4 /WX applies to our own sources only.
    cpp += [f'/imsvc{a.src / d}' for d in INCLUDE_DIRS]
    jobs = [(cpp, c, od / (c.stem + '.obj')) for c in sorted((HERE / 'src').glob('*.cpp'))]

    failed = 0
    with ThreadPoolExecutor(len(jobs)) as ex:
        for source, rc, text in ex.map(lambda j: build_db.compile_one(env, *j), jobs):
            if text:
                log(text)
            if rc != 0:
                failed += 1
                log(f'FAILED: {source}')
    log(f'compiled {len(jobs) - failed}/{len(jobs)} sources')
    if failed:
        sys.exit(1)

    dll = out / f'CvLocalization_{a.arch}.dll'
    lp = lambda q: x64build.link_path(q, out)   # issue 0115: relative paths, link in <out>
    cmd = [str(LLD_LINK), '/nologo', '/DLL', f'/machine:{a.arch}', f'/out:{lp(dll)}',
           f'/implib:{lp(dll.with_suffix(".lib"))}', f'/pdb:{lp(dll.with_suffix(".pdb"))}', f'/map:{lp(dll.with_suffix(".map"))}',
           '/DEBUG', '/DYNAMICBASE', '/NXCOMPAT', '/ERRORLIMIT:0'] + x64build.REPRO_LINK
    cmd += ['/OPT:REF', '/OPT:ICF'] if a.config == 'release' else []
    cmd += [lp(j[2]) for j in jobs] + [lp(dblib), 'kernel32.lib']
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=str(out))
    if (r.stdout + r.stderr).strip():
        log((r.stdout + r.stderr).strip())
    if r.returncode != 0:
        sys.exit(f'linking {dll} failed')
    exp = subprocess.run([str(LLVM_BIN / 'llvm-readobj.exe'), '--coff-exports', str(dll)], capture_output=True,
                         text=True).stdout
    names = [ln.split('Name: ', 1)[1].strip() for ln in exp.splitlines() if 'Name: ' in ln]
    (out / f'CvLocalization_{a.arch}.exports.txt').write_text('\n'.join(sorted(names)) + '\n')
    log(f'{dll}: {len(names)} exports, import library {dll.with_suffix(".lib").name}')
    log(f'done in {time.time() - t0:.1f} s')


if __name__ == '__main__':
    main()
