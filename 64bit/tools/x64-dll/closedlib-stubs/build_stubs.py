"""Build the x64 closed-library stub libraries (issue 1005).

Every folder `closedlib-stubs/<group>/` that holds *.cpp files is a group (discovered by glob, so new groups
only add a folder). Each group's *.cpp plus `common/*.cpp` (the stub logger) are compiled and archived into
`<out>/<group>.lib`. The `common` folder and folders starting with `_` or `.` are not groups.

The group `lua51` is special. Its own *.cpp (the FLua members, which are not Lua API) go into `<out>/lua51.lib`
like any group. The real Lua 5.1.4 core and standard libraries are compiled as C from the official lua.org tarball
(the tarball of `deps/third-party.lock`, found offline-first and SHA-256-checked by `tools/x64-dll/thirdparty.py`
through `tools/x64-dll/lua51/fetch.py`, never committed; `--lua-tarball PATH` names it explicitly, otherwise the
cache root $CV64_DEPS_CACHE is used, and a download happens only if it is missing there and $CV64_OFFLINE is not 1), with one change to luaconf.h for
x64 (see LUAC_PATCH below), and by default (`--lua dll`, issue 1009) linked as the shared `<out>/lua51_x64.dll`
with its import library `<out>/lua51_x64.lib` (LUA_BUILD_AS_DLL: the whole C API, luaL_*, luaopen_* and
luaL_openlibs are exported). This mirrors x86, where the DLL links lua51_Win32.lib, the import library of the
lua51_Win32.dll the EXE also uses: one Lua copy per process, shared by the server and the DLL. The DLL's link picks
up lua51_x64.lib with the other *.lib; the DLL needs lua51_x64.dll beside it (or on the loader path) at run time.
`--lua static` puts the Lua core into lua51.lib instead (issue 1005's form; then nobody outside the DLL can use
that Lua copy).

Toolchain as issue 1001 (tools/x64-dll/build.py): clang-cl 19.1.7 -m64, MSVC 14.43 STL/CRT via vcvars64.bat,
Windows SDK 10.0.26100.0, /std:c++14, /MD (use --crt MT only to link against /MT objects such as the
2026-11-10 probe's), the same defines and the fork's include directories. Libraries are archived with
`lld-link /lib`. Objects are built with /Zl (no default-library records): the DLL's link picks the CRT.

The project's own sources (every group's *.cpp, common/*.cpp, the generated __imp_ pointer files) compile with
/WX (warnings are errors, issue 1405); the stock Lua C sources do not.

`--src` is the DLL fork source the headers come from, checked against deps/dll.lock exactly as build.py does
(a git checkout at the locked commit, or a plain copy of it without .git; `--unlocked` for anything else).

Usage:
    python build_stubs.py --out DIR [--config debug|release] [--crt MD|MT] [--src FORK] [--unlocked]
                          [--groups a,b] [--exclude-groups a,b] [--lua dll|static] [--lua-tarball PATH] [--offline]
Outputs: <out>/<group>.lib, <out>/lua51_x64.dll/.lib/.pdb (--lua dll), <out>/obj/<group>/*.obj,
<out>/build_stubs.log, <out>/build_stubs.json. Exit 0 only when every expected output was produced; otherwise
exit 1 with a one-line reason ("build_stubs: FAILED: ...") as the last line.
"""
import argparse
import importlib.util
import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
LUA_DIR = HERE.parent / 'lua51'
_spec = importlib.util.spec_from_file_location('x64build', HERE.parent / 'build.py')
x64build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(x64build)  # resolve_src: the deps/dll.lock check
sys.path.insert(0, str(HERE.parent))
import thirdparty  # noqa: E402  tools/x64-dll/thirdparty.py

LLVM_BIN = Path(r'C:\Program Files\LLVM\bin')
CLANG_CL = LLVM_BIN / 'clang-cl.exe'
LLD_LINK = LLVM_BIN / 'lld-link.exe'
VCVARS64 = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
DEFAULT_SRC = x64build.DEFAULT_SRC  # used only if at the commit of deps/dll.lock

# Same as tools/x64-dll/build.py (issue 1001).
INCLUDE_DIRS = [
    r'CvWorldBuilderMap\include',
    r'CvGameCoreDLLUtil\include',
    r'CvLocalization\include',
    r'CvGameDatabase\include',
    r'FirePlace\include',
    r'FirePlace\include\FireWorks',
    r'ThirdPartyLibs\Lua51\include',
]
SHARED_PREDEFS = [
    'FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS',
    '_WINDLL', '_HAS_AUTO_PTR_ETC=1', '_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS', 'VP_OUT_OF_PROCESS',
    # The DLL defines this in CvGameCoreDLLPCH.h before <hash_map>; the stubs include the closed-lib headers
    # without the PCH. The second one silences std::tr1 (CvLocalization.h, Database.h).
    '_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS', '_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING',
]
PREDEFS = {
    # Must match tools/x64-dll/build.py per config: VPDEBUG adds members to FAutoVariableBase, so a mismatch
    # between the stubs and the DLL is a silent layout difference (found by issue 1006, 2026-11-10).
    'release': SHARED_PREDEFS + ['STRONG_ASSUMPTIONS', 'NDEBUG', 'VPRELEASE_ERRORMSG'],
    'debug': SHARED_PREDEFS + ['VPDEBUG'],
}
CL_SUPPRESS = ['invalid-offsetof', 'comment', 'c++11-narrowing', 'microsoft-include', 'pragma-pack',
               'unused-function']  # the closed-lib headers define static helpers the stubs never call

# Lua 5.1.4: every file of src/ except the stand-alone interpreter and compiler.
LUA_EXCLUDE = {'lua.c', 'luac.c', 'print.c'}

# x64 change to luaconf.h (applied to a copy; the fetched tree stays pristine).
# The 32-bit game's lua51_Win32.dll converts numbers to integers with x87 `fistp` (lua_tointeger at
# 0x10001610 in that DLL: fldl / fistpl), i.e. round-to-nearest-even under the default FPU control word, and
# out-of-range or NaN gives 0x80000000. Stock luaconf.h on x64 falls back to a C cast, which truncates
# (1.5 -> 1, not 2). cvtsd2si (32-bit) under the default MXCSR rounds the same way and gives the same
# 0x80000000, so this reproduces the game's results bit for bit, including keeping lua_Integer values in the
# int32 range the DLL's `int x = lua_tointeger(...)` sites assume.
LUAC_ANCHOR = '/* On a Pentium, resort to a trick */\n#if defined(LUA_NUMBER_DOUBLE)'
LUAC_PATCH = (
    '/* cv64 (c5-64bit-patch issue 1005): on x64 round like the 32-bit game\'s x87 fistp */\n'
    '#if defined(_M_X64) && defined(LUA_NUMBER_DOUBLE)\n'
    '#include <emmintrin.h>\n'
    '#define lua_number2int(i,d)\t((i)=_mm_cvtsd_si32(_mm_set_sd(d)))\n'
    '#define lua_number2integer(i,n)\t((i)=(lua_Integer)_mm_cvtsd_si32(_mm_set_sd(n)))\n'
    '\n'
    '/* On a Pentium, resort to a trick */\n'
    '#elif defined(LUA_NUMBER_DOUBLE)'
)


class Log:
    def __init__(self, path):
        self.f = open(path, 'w', encoding='utf-8')

    def __call__(self, text=''):
        print(text, flush=True)
        self.f.write(text + '\n')
        self.f.flush()


class StubBuildError(Exception):
    """A failed step: main() logs `build_stubs: FAILED: <message>` and exits 1."""


def fail(msg):
    raise StubBuildError(msg)


def vcvars_env():
    # VSCMD_SKIP_SENDTELEMETRY: vcvars otherwise starts a background PowerShell for its telemetry (bug 0108; 0147)
    out = subprocess.run(f'"{VCVARS64}" >NUL && set', shell=True, capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if out.returncode != 0:
        fail(f'vcvars64.bat failed (rc={out.returncode}): {out.stderr.strip()[:200]}')
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep:
            env[k] = v
    return env


def discover_groups(only=None, exclude=()):
    groups = []
    for d in sorted(HERE.glob('*/')):
        name = d.name
        if name == 'common' or name.startswith(('_', '.')):
            continue
        if not list(d.glob('*.cpp')) and name != 'lua51':
            continue
        if only and name not in only:
            continue
        if name in exclude:
            continue
        groups.append(name)
    return groups


def cl_base(config, crt):
    args = ['-m64', '/c', '/nologo', f'/{crt}', '/Zl', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/Z7',
            '-fdiagnostics-absolute-paths']
    args += ['/O2'] if config == 'release' else ['/Od']
    return args


def cpp_args(src, config, crt):
    args = cl_base(config, crt) + ['/std:c++14', '/W3', '/WX']  # /WX: the project's own code (issue 1405)
    args += [f'/D{d}' for d in PREDEFS[config]]
    # The fork's headers are external: their warnings (e.g. -Wreorder-ctor in FFastVector.h) belong to the fork,
    # and /WX applies to the project's own sources only.
    args += ['/external:W0'] + [f'/external:I{src / d}' for d in INCLUDE_DIRS]
    args += [f'/I{HERE / "common"}']
    args += [f'-Wno-{w}' for w in CL_SUPPRESS]
    return args


def lua_args(lua_inc, config, crt):
    # Plain C, as the game's lua51_Win32.dll was built (it imports longjmp from MSVCR90, i.e. no C++ exceptions).
    args = cl_base(config, crt) + ['/TC', '/W3', '/D_CRT_SECURE_NO_WARNINGS']
    args += ['-Wno-empty-body', '-Wno-misleading-indentation']  # stock 5.1.4 source, left untouched
    if config == 'release':
        args.append('/DNDEBUG')
    args += [f'/I{lua_inc}']
    return args


def prepare_lua(out, log, tarball=None, offline=None):
    sys.path.insert(0, str(LUA_DIR))
    import fetch  # tools/x64-dll/lua51/fetch.py
    try:
        src = Path(fetch.ensure_lua(out / 'lua51-pristine', tarball, offline))
    except thirdparty.ThirdPartyError as e:
        fail(f'Lua 5.1.4 source: {e}')
    patched = out / 'lua51-src'
    patched.mkdir(parents=True, exist_ok=True)
    for f in src.iterdir():
        if f.suffix in ('.c', '.h'):
            data = f.read_bytes().replace(b'\r\n', b'\n')
            if f.name == 'luaconf.h':
                text = data.decode('latin-1')
                if text.count(LUAC_ANCHOR) != 1:
                    fail('luaconf.h: patch anchor not found exactly once')
                data = text.replace(LUAC_ANCHOR, LUAC_PATCH).encode('latin-1')
            dst = patched / f.name
            if not dst.exists() or dst.read_bytes() != data:
                dst.write_bytes(data)
    log(f'lua51: Lua {fetch.LUA_VERSION} from {fetch.LUA_URL} (sha256 {fetch.LUA_SHA256}), patched copy in {patched}')
    return patched, sorted(p for p in patched.glob('*.c') if p.name not in LUA_EXCLUDE)


def write_imp_pointers(env, objdir, objs):
    """Write <objdir>/_imp_pointers.c: an `__imp_<name>` pointer for every C++ symbol the stub objects define.

    The closed-library headers declare their classes __declspec(dllimport), so the DLL's objects reference
    `__imp_?Open@Connection@Database@@...` (a pointer slot an import library would provide), never the symbol
    itself. lld-link 19.1.7 binds an `__imp_X` to a local X only when X is already defined; it does not pull X
    out of a static library to do so (measured 2026-11-10: all 62 CvGameDatabase/CvLocalization imports stayed
    unresolved; both groups are real shared libraries since issues 1402 and 1703). So each group library carries the pointers itself: a reference to `__imp_X` pulls this object,
    which pulls the stub object defining X. Each pointer is selectany, so two groups that both define the same
    inline or template symbol do not clash. Compiler-generated names (`??_` vtables, RTTI, string literals),
    `std::` instantiations and `type_info` are skipped: nothing imports them.
    """
    names = set()
    for o in objs:
        r = subprocess.run([str(LLVM_BIN / 'llvm-nm.exe'), '-g', '--defined-only', str(o)], env=env,
                           capture_output=True, text=True)
        if r.returncode != 0:
            fail(f'llvm-nm {o} failed: {r.stderr.strip()[:200]}')
        for line in r.stdout.splitlines():
            parts = line.split()
            if len(parts) != 3 or parts[1] not in ('T', 'D', 'B', 'R'):
                continue
            name = parts[2]
            if not name.startswith('?') or name.startswith('??_') or 'std@@' in name or 'type_info@@' in name:
                continue
            names.add(name)
    if not names:
        return None
    path = objdir / '_imp_pointers.c'
    lines = ['/* Generated by build_stubs.py (write_imp_pointers): __imp_ slots for the dllimport declarations. */']
    for i, n in enumerate(sorted(names)):
        lines.append(f'extern char cv64_s{i} __asm__("{n}");')
        lines.append(f'__declspec(selectany) void* const cv64_i{i} __asm__("__imp_{n}") = &cv64_s{i};')
    path.write_text('\n'.join(lines) + '\n')
    return path


def link_lua_dll(env, out, dll, objs, config, log):
    """Link lua51_x64.dll and its import library lua51_x64.lib from the LUA_BUILD_AS_DLL objects.

    The objects are /Zl, so the CRT is named here: /MD import libraries (msvcrt, vcruntime, ucrt), the same CRT
    DLLs the game DLL and the server use. Exports come from the __declspec(dllexport) of LUA_API/LUALIB_API.
    """
    lp = lambda q: x64build.link_path(q, out)   # issue 0115: relative paths, link in <out>
    cmd = [str(LLD_LINK), '/nologo', '/DLL', '/machine:x64', f'/out:{lp(dll)}', f'/implib:{lp(dll.with_suffix(".lib"))}',
           f'/pdb:{lp(dll.with_suffix(".pdb"))}', f'/map:{lp(dll.with_suffix(".map"))}', '/DEBUG', '/DYNAMICBASE',
           '/NXCOMPAT', '/ERRORLIMIT:0'] + x64build.REPRO_LINK + (['/OPT:REF', '/OPT:ICF'] if config == 'release' else [])
    cmd += [lp(o) for o in objs] + ['msvcrt.lib', 'vcruntime.lib', 'ucrt.lib', 'kernel32.lib']
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=str(out))
    if (r.stdout + r.stderr).strip():
        log((r.stdout + r.stderr).strip())
    if r.returncode != 0:
        fail(f'linking {dll} failed (rc={r.returncode})')
    exports = subprocess.run([str(LLVM_BIN / 'llvm-readobj.exe'), '--coff-exports', str(dll)], capture_output=True,
                             text=True).stdout.count('Name: ')
    log(f'{dll}: {len(objs)} objects, {exports} exports, import library {dll.with_suffix(".lib").name}')


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
    cmd = [str(CLANG_CL)] + tail
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=cwd)
    return source, r.returncode, (r.stdout + r.stderr).strip()


def build(a, log, out):
    t0 = time.time()
    try:
        srcinfo = x64build.resolve_src(a.src, a.unlocked)
    except x64build.SrcError as e:
        fail(str(e))
    src = srcinfo['src']
    exclude = set(a.exclude_groups.split(',')) if a.exclude_groups else set()
    groups = discover_groups(set(a.groups.split(',')) if a.groups else None, exclude)
    if not groups:
        fail('no stub group selected')
    log(f'build_stubs: config={a.config} crt=/{a.crt} out={out}')
    log(f'src: {src} ({srcinfo["note"]})')
    log(f'groups: {", ".join(groups)}' + (f'; excluded: {", ".join(sorted(exclude))}' if exclude else ''))

    # Drop this script's previous outputs first, so a failed run never leaves an older library behind for
    # build.py --stubs to link. Files of other producers in --out (CvGameDatabase_x64.*, CvLocalization_x64.*) are not touched.
    stale = [out / f'{g}.lib' for g in groups] + [out / f'{g}.lib' for g in exclude] + [out / 'build_stubs.json']
    if 'lua51' in groups:
        stale += [out / f'lua51_x64{ext}' for ext in ('.dll', '.lib', '.pdb', '.exp', '.map')]
    for p in stale:
        if p.exists():
            p.unlink()
            if p.stem in exclude:
                log(f'removed {p.name} (group excluded)')

    env = x64build.repro_env(vcvars_env())   # issue 0115
    REPRO.update(out=out, src=Path(src))
    ver = subprocess.run([str(CLANG_CL), '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    log(f'clang-cl: {ver}; MSVC {env.get("VCToolsVersion", "?")}; SDK {env.get("WindowsSDKVersion", "?").strip(chr(92))}')

    jobs = []  # (group, args, source, obj)
    common_obj = out / 'obj' / '_common'
    common_obj.mkdir(parents=True, exist_ok=True)
    common = [(None, cpp_args(src, a.config, a.crt), c, common_obj / (c.stem + '.obj'))
              for c in sorted((HERE / 'common').glob('*.cpp'))]
    jobs += common
    members = {g: [j[3] for j in common] for g in groups}
    lua_dll_objs = []
    for g in groups:
        od = out / 'obj' / g
        od.mkdir(parents=True, exist_ok=True)
        for c in sorted((HERE / g).glob('*.cpp')):
            o = od / (c.stem + '.obj')
            jobs.append((g, cpp_args(src, a.config, a.crt), c, o))
            members[g].append(o)
        if g == 'lua51':
            lua_inc, lua_c = prepare_lua(out, log, a.lua_tarball, True if a.offline else None)
            if a.lua == 'dll':
                lod = od / 'dll'
                lod.mkdir(exist_ok=True)
                for c in lua_c:
                    o = lod / ('lua_' + c.stem + '.obj')
                    jobs.append(('lua51_x64.dll', lua_args(lua_inc, a.config, a.crt) + ['/DLUA_BUILD_AS_DLL'], c, o))
                    lua_dll_objs.append(o)
            else:
                for c in lua_c:
                    o = od / ('lua_' + c.stem + '.obj')
                    jobs.append((g, lua_args(lua_inc, a.config, a.crt), c, o))
                    members[g].append(o)
            # The FLua members include the fork's lua.h; the Lua core uses the stock headers. They agree on
            # every type and number setting (the fork adds only LUA_NORETURN and a longer LUA_CPATH_DEFAULT).

    failed = []
    with ThreadPoolExecutor(a.jobs) as ex:
        for source, rc, text in ex.map(lambda j: compile_one(env, j[1], j[2], j[3]), jobs):
            if text:
                log(text)
            if rc != 0:
                failed.append(source.name)
                log(f'FAILED: {source}')
    log(f'compiled {len(jobs) - len(failed)}/{len(jobs)} sources')
    if failed:
        fail(f'{len(failed)} source(s) failed to compile ({", ".join(failed[:5])}{", ..." if len(failed) > 5 else ""}); '
             f'see {out / "build_stubs.log"}')

    # __imp_ pointers: one compiled C file per group (see write_imp_pointers).
    for g in groups:
        stub_objs = [j[3] for j in jobs if j[0] == g and j[2].suffix == '.cpp']
        imp_c = write_imp_pointers(env, out / 'obj' / g, stub_objs)
        if imp_c is None:
            continue
        imp_o = imp_c.with_suffix('.obj')
        _, rc, text = compile_one(env, cl_base(a.config, a.crt) + ['/TC', '/W3', '/WX'], imp_c, imp_o)
        if text:
            log(text)
        if rc != 0:
            fail(f'compiling {imp_c} failed')
        members[g].append(imp_o)
        log(f'{g}: {sum(1 for l in open(imp_c) if l.startswith("extern"))} __imp_ pointers')

    manifest = {}
    lua_dll = out / 'lua51_x64.dll'
    expected = [out / f'{g}.lib' for g in groups]
    if lua_dll_objs:
        link_lua_dll(env, out, lua_dll, lua_dll_objs, a.config, log)
        manifest['lua51_x64.dll'] = {'dll': str(lua_dll), 'implib': str(lua_dll.with_suffix('.lib')),
                                     'members': [m.name for m in lua_dll_objs]}
        expected += [lua_dll, lua_dll.with_suffix('.lib')]
    for g in groups:
        lib = out / f'{g}.lib'
        cmd = [str(LLD_LINK), '/lib', '/nologo', '/machine:x64', f'/out:{x64build.link_path(lib, out)}'] + \
            [x64build.link_path(m, out) for m in members[g]]   # issue 0115: member names relative
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=str(out))
        if r.returncode != 0:
            log((r.stdout + r.stderr).strip())
            fail(f'archiving {lib} failed (rc={r.returncode})')
        manifest[g] = {'lib': str(lib), 'members': [m.name for m in members[g]]}
        log(f'{lib}: {len(members[g])} objects')
    missing = [p.name for p in expected if not p.is_file() or p.stat().st_size == 0]
    if missing:
        fail(f'expected output(s) missing after the build: {", ".join(missing)}')
    (out / 'build_stubs.json').write_text(json.dumps({'config': a.config, 'crt': a.crt, 'lua': a.lua,
                                                      'src': str(src), 'src_mode': srcinfo['mode'],
                                                      'src_commit': srcinfo['head'], 'groups': manifest}, indent=1))
    log(f'done in {time.time() - t0:.1f} s: {len(expected)} outputs ({", ".join(p.name for p in expected)})')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--crt', choices=['MD', 'MT'], default='MD')
    ap.add_argument('--src', type=Path, default=None,
                    help=f'DLL fork source (headers): git checkout at the deps/dll.lock commit or a plain copy of it; '
                         f'default {DEFAULT_SRC} if it is at that commit')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit (fork development)')
    ap.add_argument('--groups', help='comma-separated subset of groups')
    ap.add_argument('--exclude-groups', help='comma-separated groups to leave out (their old .lib is removed)')
    ap.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--lua', choices=['dll', 'static'], default='dll',
                    help='Lua 5.1.4 as the shared lua51_x64.dll + import lib (default, issue 1009) or inside lua51.lib')
    ap.add_argument('--lua-tarball', type=Path,
                    help='lua-5.1.4.tar.gz to use (SHA-256 checked against deps/third-party.lock); default: the cache '
                         'root $CV64_DEPS_CACHE')
    ap.add_argument('--offline', action='store_true', help='never download (same as CV64_OFFLINE=1)')
    a = ap.parse_args()
    if a.lua == 'dll' and a.crt != 'MD':
        sys.exit('build_stubs: FAILED: --lua dll needs --crt MD (the Lua DLL, the game DLL and the server share one '
                 'CRT heap)')

    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = Log(out / 'build_stubs.log')
    try:
        build(a, log, out)
    except StubBuildError as e:
        log(f'build_stubs: FAILED: {e}')
        sys.exit(1)
    except Exception as e:  # noqa: BLE001 - any other failure is a failed build too, never exit 0
        log(f'build_stubs: FAILED: unexpected {type(e).__name__}: {e}')
        raise


if __name__ == '__main__':
    main()
