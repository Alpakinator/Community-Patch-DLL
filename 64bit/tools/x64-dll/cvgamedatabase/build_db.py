"""Build the CvGameDatabase replacement (issues 1402, 1403): a shared DLL plus import library over SQLite 3.7.17.

    python build_db.py --out DIR [--arch x64|x86] [--config debug|release] [--src FORK] [--unlocked]
                       [--sqlite-zip PATH]

`--src` (the fork's headers) is checked against deps/dll.lock by tools/x64-dll/build.py's resolve_src, like every
script here (issue 1508): a git checkout at the locked commit without tracked changes, or a plain copy whose
fingerprint matches; `--unlocked` accepts another git commit on purpose. Default: build.py's DEFAULT_SRC, only
while it is at the locked commit.

Writes DIR/CvGameDatabase_<arch>.dll, .lib (import library), .pdb, .map, and DIR/build_db.log. Objects go to
DIR/obj-db-<arch>-<config>/, the SQLite amalgamation is unpacked to DIR/sqlite-3.7.17/ (never into the repository).

Why a DLL: the server opens the Database::Connection and hands it to the game DLL through SetGameDatabase, so both
must use one copy of this wrapper and one SQLite per process (as lua51_x64.dll). The classes are exported with
`CvGameDatabaseAPI = __declspec(dllexport)`, so the export names are exactly the ones the game DLL's `__imp_`
references (x64) and the closed CvGameDatabaseWin32.lib (x86) carry. tools/x64-dll/build.py --stubs DIR links every
*.lib in DIR, so `--out <stubs dir>` makes the game DLL pick this import library up.

Toolchain as closedlib-stubs/build_stubs.py: clang-cl 19.1.7, MSVC 14.4x STL/CRT via vcvars64.bat (vcvars32.bat for
x86), /std:c++14, /MD, the same per-config defines (they must match the game DLL's: VPDEBUG changes layouts) and the
fork's include directories. Our C++ compiles with /W4 /WX; the SQLite amalgamation is compiled as C with the
compile options found in the closed library (see README.md).
"""
import argparse
import importlib.util
import os
import subprocess
import sys
import time
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('x64build', HERE.parent / 'build.py')
x64build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(x64build)  # resolve_src: the deps/dll.lock check
sys.path.insert(0, str(HERE.parent))
import thirdparty  # noqa: E402  tools/x64-dll/thirdparty.py

LLVM_BIN = Path(r'C:\Program Files\LLVM\bin')
CLANG_CL = LLVM_BIN / 'clang-cl.exe'
LLD_LINK = LLVM_BIN / 'lld-link.exe'
VCVARS = {
    'x64': Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'),
    'x86': Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat'),
}
DEFAULT_SRC = x64build.DEFAULT_SRC  # used only if at the commit of deps/dll.lock

# Same as closedlib-stubs/build_stubs.py and tools/x64-dll/build.py (issue 1001).
INCLUDE_DIRS = [r'CvGameDatabase\include']
SHARED_PREDEFS = [
    'FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS',
    '_WINDLL', '_HAS_AUTO_PTR_ETC=1', '_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS', 'VP_OUT_OF_PROCESS',
    '_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS', '_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING',
]
PREDEFS = {
    'release': SHARED_PREDEFS + ['STRONG_ASSUMPTIONS', 'NDEBUG', 'VPRELEASE_ERRORMSG'],
    'debug': SHARED_PREDEFS + ['VPDEBUG'],
}

SQLITE_DIRNAME = 'sqlite-amalgamation-3071700'
# The closed library's sqlite3_compileoption_get list (strings in CvGameDatabaseWin32Final Release.dll, SHA-256
# f95637398ce10012..., read 2026-11-10): THREADSAFE=1, TEMP_STORE=2, OMIT_TCL_VARIABLE, OMIT_PROGRESS_CALLBACK,
# OMIT_DEPRECATED, OMIT_AUTOINIT, OMIT_AUTHORIZATION, version 3.7.17 (2013-05-20 118a3b35...). Same here.
SQLITE_DEFINES = [
    'SQLITE_THREADSAFE=1', 'SQLITE_TEMP_STORE=2', 'SQLITE_OMIT_TCL_VARIABLE', 'SQLITE_OMIT_PROGRESS_CALLBACK',
    'SQLITE_OMIT_DEPRECATED', 'SQLITE_OMIT_AUTOINIT', 'SQLITE_OMIT_AUTHORIZATION',
]
SQLITE_WNO = ['unused-but-set-variable', 'unused-function', 'unused-variable', 'unused-parameter',
              'sign-compare', 'missing-field-initializers', 'implicit-fallthrough', 'deprecated-declarations',
              'unused-const-variable', 'misleading-indentation', 'int-to-void-pointer-cast',
              'pointer-to-int-cast', 'empty-body', 'unknown-pragmas', 'implicit-const-int-float-conversion']


class Log:
    def __init__(self, path):
        self.f = open(path, 'w', encoding='utf-8')

    def __call__(self, text=''):
        print(text)
        self.f.write(text + '\n')
        self.f.flush()


def vcvars_env(arch):
    # VSCMD_SKIP_SENDTELEMETRY: vcvars otherwise starts a background PowerShell for its telemetry (bug 0108; 0147)
    out = subprocess.run(f'"{VCVARS[arch]}" >NUL && set', shell=True, capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if out.returncode != 0:
        sys.exit(f'{VCVARS[arch].name} failed: {out.stderr}')
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep:
            env[k] = v
    return env


def unpack_sqlite(zip_path, out):
    dest = out / 'sqlite-3.7.17'
    src = dest / SQLITE_DIRNAME
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            target = dest / info.filename
            if not target.resolve().is_relative_to(dest.resolve()):
                sys.exit(f'{zip_path}: unsafe member {info.filename}')
            data = z.read(info)
            if not target.exists() or target.read_bytes() != data:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
    if not (src / 'sqlite3.c').is_file():
        sys.exit(f'{zip_path}: {SQLITE_DIRNAME}/sqlite3.c not found')
    return src


def base_args(arch, config):
    args = ['-m64' if arch == 'x64' else '-m32', '/c', '/nologo', '/MD', '/GS', '/EHsc', '/fp:precise',
            '/Zc:wchar_t', '/Z7', '-fdiagnostics-absolute-paths']
    args += ['/O2'] if config == 'release' else ['/Od']
    return args


# Issue 0115 (repeatable builds): the folders the compile and link run against; set by main() before any compile.
REPRO = {"out": None, "src": None}


def _repro_compile(args, source, obj):
    """(cmd tail, cwd): the source by its name relative to its root, its S_OBJNAME relative to the output folder,
    every folder under a fixed name in the compiler's strings (x64build.repro_cl_args)."""
    repo = HERE.parents[2]
    cwd, rel = x64build.source_cwd(source, REPRO["src"], REPRO["out"], repo)
    extra = x64build.repro_cl_args((REPRO["src"], 'VPSRC'), (REPRO["out"], 'VPOUT'), (repo, 'VPREPO'))
    if REPRO["out"]:
        extra += x64build.objname_args(obj, REPRO["out"])
    return list(args) + extra + [rel, f'/Fo{obj}'], cwd


def compile_one(env, args, source, obj):
    tail, cwd = _repro_compile(args, source, obj)
    r = subprocess.run([str(CLANG_CL)] + tail, env=env, capture_output=True, text=True, cwd=cwd)
    return source, r.returncode, (r.stdout + r.stderr).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--arch', choices=['x64', 'x86'], default='x64')
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source (headers): git checkout at the deps/dll.lock commit or a plain copy of it; '
                         f'default {DEFAULT_SRC} if it is at that commit')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit (fork development)')
    ap.add_argument('--sqlite-zip', type=Path, help='explicit sqlite-amalgamation-3071700.zip (else the cache)')
    a = ap.parse_args()
    try:
        a.src = x64build.resolve_src(a.src, a.unlocked)['src']
    except x64build.SrcError as e:
        sys.exit(f'build_db: FAILED: {e}')

    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = Log(out / f'build_db_{a.arch}.log')
    t0 = time.time()
    try:
        zip_path = thirdparty.ensure('sqlite', explicit=a.sqlite_zip)
    except thirdparty.ThirdPartyError as e:
        log(f'error: {e}')
        sys.exit(2)
    log(f'build_db: arch={a.arch} config={a.config} src={a.src} out={out}')
    log(f'sqlite: {zip_path} (sha256 {thirdparty.sha256(zip_path)})')
    sq = unpack_sqlite(zip_path, out)
    env = x64build.repro_env(vcvars_env(a.arch))   # issue 0115
    REPRO.update(out=out, src=Path(a.src))
    ver = subprocess.run([str(CLANG_CL), '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    log(f'clang-cl: {ver}; MSVC {env.get("VCToolsVersion", "?")}; '
        f'SDK {env.get("WindowsSDKVersion", "?").strip(chr(92))}')

    od = out / f'obj-db-{a.arch}-{a.config}'
    od.mkdir(parents=True, exist_ok=True)
    jobs = []
    sq_args = base_args(a.arch, a.config) + ['/TC', '/W3'] + [f'/D{d}' for d in SQLITE_DEFINES]
    sq_args += [f'-Wno-{w}' for w in SQLITE_WNO]
    if a.config == 'release':
        sq_args.append('/DNDEBUG')
    jobs.append((sq_args, sq / 'sqlite3.c', od / 'sqlite3.obj'))
    cpp = base_args(a.arch, a.config) + ['/std:c++14', '/W4', '/WX', '/DCvGameDatabaseAPI=__declspec(dllexport)']
    cpp += [f'/D{d}' for d in PREDEFS[a.config]]
    # The fork's headers and sqlite3.h are system includes: /W4 /WX applies to our own sources only.
    cpp += [f'/imsvc{a.src / d}' for d in INCLUDE_DIRS] + [f'/imsvc{sq}']
    for c in sorted((HERE / 'src').glob('*.cpp')):
        jobs.append((cpp, c, od / (c.stem + '.obj')))

    failed = 0
    with ThreadPoolExecutor(len(jobs)) as ex:
        for source, rc, text in ex.map(lambda j: compile_one(env, *j), jobs):
            if text:
                log(text)
            if rc != 0:
                failed += 1
                log(f'FAILED: {source}')
    log(f'compiled {len(jobs) - failed}/{len(jobs)} sources')
    if failed:
        sys.exit(1)

    dll = out / f'CvGameDatabase_{a.arch}.dll'
    lp = lambda q: x64build.link_path(q, out)   # issue 0115: relative paths, link in <out>
    cmd = [str(LLD_LINK), '/nologo', '/DLL', f'/machine:{a.arch}', f'/out:{lp(dll)}',
           f'/implib:{lp(dll.with_suffix(".lib"))}', f'/pdb:{lp(dll.with_suffix(".pdb"))}', f'/map:{lp(dll.with_suffix(".map"))}',
           '/DEBUG', '/DYNAMICBASE', '/NXCOMPAT', '/ERRORLIMIT:0'] + x64build.REPRO_LINK
    cmd += ['/OPT:REF', '/OPT:ICF'] if a.config == 'release' else []
    cmd += [lp(j[2]) for j in jobs] + ['kernel32.lib']
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=str(out))
    if (r.stdout + r.stderr).strip():
        log((r.stdout + r.stderr).strip())
    if r.returncode != 0:
        sys.exit(f'linking {dll} failed')
    exp = subprocess.run([str(LLVM_BIN / 'llvm-readobj.exe'), '--coff-exports', str(dll)], capture_output=True,
                         text=True).stdout
    names = [ln.split('Name: ', 1)[1].strip() for ln in exp.splitlines() if 'Name: ' in ln]
    (out / f'CvGameDatabase_{a.arch}.exports.txt').write_text('\n'.join(sorted(names)) + '\n')
    sqlite_exports = [n for n in names if n.startswith('sqlite3')]
    log(f'{dll}: {len(names)} exports ({len(sqlite_exports)} sqlite3_*), import library {dll.with_suffix(".lib").name}')
    log(f'done in {time.time() - t0:.1f} s')


if __name__ == '__main__':
    main()
