"""Build the x64 server (epic 1500, issue 1501) in one command.

    python server/build_server.py --src <fork source> --out <dir> [--config debug|release] [--deps <build_all out>]
                                  [--offline] [--unlocked]

Steps:
1. The DLL and its x64 dependencies: `tools/x64-dll/build_all.py --src <src> --out <out>/deps` (stubs, lua51_x64.dll,
   CvGameDatabase_x64.dll, CvLocalization_x64.dll, the DLL, smoke host and smoke run), unless `--deps` names a folder a previous build_all
   run of the same `--config` produced (its `build_all.json` must say every step passed).
2. The server: generate the typed interface overrides from the fork's headers (`tools/x64-dll/smoke-host/
   gen_ifaces.py`), compile `server/src/*.cpp` with the DLL's toolchain, `/MD` and defines (D15; own code with /WX,
   fork headers as external headers), link `server.exe` (console, exports the 22 host exports; Lua, the database and
   the localization library through their import libraries, delay-loaded, so the server and the DLL share one copy
   of each; issue 1703 added CvLocalization). Issue 2704: `<out>/server/gen/vp_build_info.h` (proxy/gen/build_info.py:
   repo commit, dirty flag, DLL pin) goes into server.log's first line with server.exe's own SHA-256 (src/srv_buildid.cpp).
   Issue 2801: from a copy of this repository without .git (an archive) the repo field is "<commit> (archive)" from
   `--repo-commit` or `<repo>/.repo-commit`, else "unknown (archive: ...)" with a LOUD line (proxy/build_proxy.py
   apply_repo_identity, also passed on to the proxy build).
   zlib 1.2.3 (issue 2001: the game's zlib1.dll version; deps/third-party.lock `[zlib]`, unpacked into
   `<out>/zlib/`, its C files compiled with the same clang-cl, /MD) is linked statically into server.exe for the
   .Civ5Save container (src/srv_civ5save.cpp); the same objects and srv_civ5save.cpp also make `civ5save.exe`
   (src/civ5save-tool/, the native container as a command-line tool; no DLL needed).
   Remote mode (epic 2300, task 2303; server.exe --remote): the generator `proxy/gen/gen_runtime.py` writes the
   generated runtime into `<out>/gen` (hooks scanned in proxy/src and server/src/remote), and server.exe also gets
   `server/src/remote/*.cpp`, the generated `<out>/gen/server/*.cpp` and `<out>/gen/common/vp_ids.gen.cpp` (no /WX
   for generated code) and the vpipc transport `ipc/src/*.cpp` (its own flags: W4 /WX, C++14). All translation units
   compile in parallel.
   The renderer (epic 3300, issue 3301): `render/build.py --target c5render` (its own launch gate) builds the c5render
   library into `<out>/render`; `server/src/render/*.cpp` and `shell/frame/ring_sink.cpp` compile WITHOUT the DLL fork's
   include path (`render_compile_args`, written to `<out>/server/render-compile-args.json` for the tests), and server.exe
   links c5render with d3d11, dxgi and d3dcompiler delay-loaded (a server with render=0 never loads them).
   The EXE build table (ticket 4960, epic 0590, D202): `tools/exebuild/emit_header.py --out <out>/gen-exebuild` writes
   vp_exe_tables.gen.h from the local per-build tables (setting exe_tables) before any compile; every unit (render units
   too) has `exebuild/include` and `<out>/gen-exebuild` on its include path. Without the tables the build fails, naming
   the setting.
3. `<out>/bin`: server.exe, civ5save.exe, CvGameCore_Expansion2.dll, lua51_x64.dll, CvGameDatabase_x64.dll,
   CvLocalization_x64.dll and their PDBs, ready for
   `server/run_server.py --bin <out>/bin`.
4. The x86 proxy (task 2302): `proxy/build_proxy.py --out <out>/proxy --src <src> --gen <out>/gen --config <config>`
   when that script exists (the same generated tree, so both sides share one kAppSchemaHash); the proxy lands in
   `<out>/proxy/bin`. `--no-proxy` skips it; a missing script is a failure (issue 9008; it used to be skipped with a
   note and the build ended OK).

`--civ5save-only` builds just zlib and `<out>/bin/civ5save.exe` (no --src, no DLL), for the container tests.

`--heap-type segment` (ticket 5037, the allocator trial) embeds an application manifest that makes the process heap
the Segment Heap (`SEGMENT_HEAP_MANIFEST`); the default links no manifest, as before.

`--test-dialog` (issue 2501) builds a TEST server with `/DVP_SERVER_TEST_DIALOG` (`server.exe --test-dialog KINDS`
shows NULL-owner message boxes after installing the window guard, `--test-no-guard` skips the guard; srv_guard.cpp)
into `<out>/server-testdialog` and `<out>/bin-testdialog`, never into `<out>/bin`, and builds no proxy. The normal
build never has the define.

Rules as every tools/x64-dll script (issue 1508): `--src` is checked against deps/dll.lock (git checkout at the locked
commit without tracked changes, or a plain copy whose source fingerprint matches); third-party archives only from
deps/third-party.lock, offline with `--offline`; outputs only under `--out` (outside the repository); nothing is
written into `--src`. Exit 0 on success, 1 on failure with a last line `build_server: FAILED: <reason>`.
"""
import argparse
import concurrent.futures
import importlib.util
import os
import json
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
X64 = REPO / 'tools' / 'x64-dll'
spec = importlib.util.spec_from_file_location('x64build', X64 / 'build.py')
x64build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(x64build)
sys.path.insert(0, str(X64 / 'smoke-host'))
import gen_ifaces  # noqa: E402
sys.path.insert(0, str(X64))
import thirdparty  # noqa: E402  tools/x64-dll/thirdparty.py
sys.path.insert(0, str(REPO / 'proxy' / 'gen'))
import build_info  # noqa: E402  proxy/gen/build_info.py (issue 2704)
import linkcheck  # noqa: E402  tools/x64-dll/linkcheck.py (one-off 9023, P10)

#: The success lines of the sub-builds (one-off 9023, P10): exit 0 without them is a failure.
BUILD_ALL_OK = r'^build_all: OK\b'
BUILD_PROXY_OK = r'^build_proxy: OK\b'


def _apply_repo_identity(*args):
    """proxy/build_proxy.py apply_repo_identity (issue 2801), loaded on first use."""
    spec_bp = importlib.util.spec_from_file_location('build_proxy_mod', REPO / 'proxy' / 'build_proxy.py')
    bp = importlib.util.module_from_spec(spec_bp)
    spec_bp.loader.exec_module(bp)
    return bp.apply_repo_identity(*args)

# zlib 1.2.3's library sources (the tarball's top level; not example.c, minigzip.c, gzio.c: no gz file API needed).
ZLIB_C = ['adler32', 'compress', 'crc32', 'deflate', 'infback', 'inffast', 'inflate', 'inftrees', 'trees', 'uncompr',
          'zutil']

NOT_FOR_HOST = {'_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS'}

# Issue 3301 (epic 3300): the renderer inside server.exe. Its units see only these include folders (no DLL fork path).
RENDER_INCLUDES = [HERE / 'src' / 'render', HERE / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
                   REPO / 'render' / 'include', REPO / 'options' / 'include',   # issue 3312: srv_options.h of the renderer
                   REPO / 'models' / 'include']   # epic 0375, issue 4454: c5r/units/draw.h includes c5models/gr2.h
RENDER_LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib']
RENDER_DELAYLOAD = ['d3d11.dll', 'dxgi.dll', 'd3dcompiler_47.dll']


def render_sources():
    """The render units of server.exe: server/src/render/*.cpp, the ring sink of shell/frame (issue 3302), the server's
    unit scene server/src/units/*.cpp (epic 0375, issue 4454: stems srv_units_*, unique among the render units, because
    objects are named by stem in obj/render), the forest scene server/src/forest/*.cpp (epic 0431, ticket 4554: stems
    srv_forest_*; it needs c5r_models.lib as the units do) and the model module server/src/render/landmarks/*.cpp and
    server/src/render/cities/*.cpp (epic 0561, ticket 4875: stems srv_model*)."""
    srcs = sorted((HERE / 'src' / 'render').glob('*.cpp')) + [REPO / 'shell' / 'frame' / 'ring_sink.cpp']
    srcs += sorted((HERE / 'src' / 'units').glob('*.cpp'))
    srcs += sorted((HERE / 'src' / 'effects').glob('*.cpp'))   # epic 0042, ticket 4722: the trigger stream (stems srv_trigger_*)
    # epic 0455, issue 4981: the unit extras module (stems srv_units_extras*, unique among the render units); it registers
    # its factory with the unit scene (srvunits::RegisterUnitsExtrasFactory)
    srcs += sorted((HERE / 'src' / 'extras' / 'units').glob('*.cpp'))
    srcs += sorted((HERE / 'src' / 'forest').glob('*.cpp'))
    # epic 0561, ticket 4875: the model module (stems srv_model*_, unique among the render units)
    srcs += sorted((HERE / 'src' / 'render' / 'landmarks').glob('*.cpp'))
    srcs += sorted((HERE / 'src' / 'render' / 'cities').glob('*.cpp'))
    # epic 0454, ticket 4938: the extras module (stems srv_extra*, unique among the render units); it registers its
    # factory with srv_render_scene.cpp, so a program without it draws no extra
    srcs += sorted((HERE / 'src' / 'extras').glob('*.cpp'))
    stems = [p.stem for p in srcs]
    dup = sorted({s for s in stems if stems.count(s) > 1})
    if dup:
        fail('render units with the same stem (one object name in obj/render): ' + ', '.join(dup))
    return srcs


def render_compile_args(config, out):
    """clang-cl flags of the render units: the server's own flags (C++14, /MD, /W3 /WX) without the DLL fork's include
    path and without the DLL's predefines."""
    r = ['-m64', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/Zi', '/FS', '/std:c++14',
         '-fdiagnostics-absolute-paths', '/W3', '/WX', '/D_CRT_SECURE_NO_WARNINGS']
    r += ['/O2', '/DNDEBUG'] if config == 'release' else ['/Od', '/Oy-']
    r += x64build.repro_cl_args((REPO, 'VPREPO'), (out, 'VPOUT'))   # issue 0115
    return r + [f'/I{p}' for p in RENDER_INCLUDES] + exebuild_includes(out)


def render_args_see_dll(nargs, src) -> bool:
    """True when a render unit's arguments name a DLL header folder (x64build.INCLUDE_DIRS under the DLL source).

    Ticket 4706: was a substring test of the whole --src folder, which refused every build in the upstream layout,
    where this repository is `64bit/` inside the DLL source, so each render include path (`<src>/64bit/server/src/...`)
    contains the source folder. The guard's purpose is unchanged: the render units must not see the DLL's headers.
    Compared without regard to case or slash direction (Windows paths)."""
    def norm(s):
        return str(s).replace('/', '\\').lower()
    dirs = [norm(Path(src) / d) for d in x64build.INCLUDE_DIRS]
    return any(d in norm(x) for d in dirs for x in nargs)


# Ticket 4960 (epic 0590, D202): the EXE address header vp_exe_tables.gen.h, written from the local per-build tables
# (setting exe_tables) into <out>/gen-exebuild by tools/exebuild/emit_header.py; exebuild/include holds
# vp_exe_build.h, vp_exe_symbols.h and vp_exe_patterns.h.
EXEBUILD_INCLUDE = REPO / 'exebuild' / 'include'


def exebuild_includes(out):
    # ticket 4966: VP_REQUIRE_EXE_TABLES makes a missing generated header a compile error in the product (test builders
    # of server/src/render without it get a build that refuses every EXE: srv_liveterrain_exefield.cpp)
    return [f'/I{EXEBUILD_INCLUDE}', f'/I{Path(out) / "gen-exebuild"}', '/DVP_REQUIRE_EXE_TABLES']


def emit_exe_tables(out, say):
    """vp_exe_tables.gen.h into <out>/gen-exebuild; fails the build naming the setting when the tables are missing."""
    cmd = [sys.executable, '-I', str(REPO / 'tools' / 'exebuild' / 'emit_header.py'), '--out', str(out / 'gen-exebuild')]
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    say((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0:
        fail(f'the EXE address header (tools/exebuild/emit_header.py): {(cp.stdout + cp.stderr).strip()}')


def build_c5render(out, say):
    """render/'s static library c5render (render/build.py --target c5render, Release, its own launch gate) into
    <out>/render; returns the .lib path. Same toolchain as the server (clang-cl, C++14, /MD)."""
    rdir = out / 'render'
    cmd = [sys.executable, '-I', str(REPO / 'render' / 'build.py'), '--build-dir', str(rdir), '--target', 'c5render']
    say('build_server: ' + ' '.join(cmd))
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', cwd=str(REPO))
    say((cp.stdout + cp.stderr).rstrip()[-3000:], echo=cp.returncode != 0)
    lib = rdir / 'c5render.lib'
    if cp.returncode != 0 or not lib.exists():
        fail(f'render/build.py --target c5render (exit {cp.returncode}; see {out / "server" / "build_server.log"})')
    return lib


def c5r_models_lib(c5render):
    """epic 0375, issue 4454: the models library 0041 builds beside c5render (render/src/units/CMakeLists.txt: c5r_models, a
    link dependency of c5render, so --target c5render builds it) at <render build>/src/units/c5r_models.lib."""
    lib = c5render.parent / 'src' / 'units' / 'c5r_models.lib'
    if not lib.exists():
        fail(f'{lib} missing (render/build.py --target c5render builds it with c5render)')
    return lib
INIT_LOCKS_X64 = '??4_Init_locks@std@@QEAAAEAV01@AEBV01@@Z'


def fail(msg):
    print(f'build_server: FAILED: {msg}', flush=True)
    sys.exit(1)


def unpack_zlib(out, tarball, offline):
    """Verified zlib tarball (deps/third-party.lock) unpacked under <out>/zlib; returns the source directory."""
    e = thirdparty.read_lock()['zlib']
    tgz = thirdparty.ensure('zlib', explicit=tarball, offline=offline)
    root = out / 'zlib'
    src = root / f'zlib-{e["version"]}'
    stamp = root / ('.verified-' + e['sha256'].lower()[:16])
    if not stamp.exists():
        root.mkdir(parents=True, exist_ok=True)
        with tarfile.open(tgz, 'r:gz') as tf:
            tf.extractall(root, filter='data')  # no absolute paths, no links out of the tree, no special files
        stamp.write_text(e['sha256'].lower() + '\n')
    if not (src / 'deflate.c').exists():
        raise thirdparty.ThirdPartyError(f'unpacked tree at {src} has no deflate.c')
    return src


def build_zlib(out, config, env, say, zsrc):
    """zlib's C files -> objects in <out>/zlib/obj (stock source, untouched; warnings off, as other third-party C)."""
    odir = out / 'zlib' / 'obj'
    odir.mkdir(parents=True, exist_ok=True)
    args = ['-m64', '/c', '/nologo', '/MD', '/GS', '/Z7', '/TC', '/W0', '/DNDEBUG', '/D_CRT_SECURE_NO_WARNINGS',
            '/D_CRT_NONSTDC_NO_DEPRECATE', f'/I{zsrc}']
    args += x64build.repro_cl_args((out, 'VPOUT'))   # issue 0115: no build folder in the binaries
    args += ['/O2'] if config == 'release' else ['/Od']
    objs = []
    for n in ZLIB_C:
        obj = odir / (n + '.obj')
        cwd, rel = x64build.source_cwd(zsrc / (n + '.c'), out)   # issue 0115: relative source name
        cp = subprocess.run([str(x64build.CLANG_CL)] + args + x64build.objname_args(obj, out) +
                            [f'/Fo{obj}', rel], capture_output=True,
                            text=True, errors='replace', env=env, cwd=cwd)
        say(f'compile zlib {n}.c: rc={cp.returncode}', echo=cp.returncode != 0)
        if cp.returncode != 0:
            say((cp.stdout + cp.stderr).rstrip())
            fail(f'zlib {n}.c did not compile')
        objs.append(obj)
    return objs


def build_civ5save_tool(sdir, config, env, say, zsrc, zobjs):
    """civ5save.exe: src/srv_civ5save.cpp + src/civ5save-tool/civ5save_main.cpp + zlib (own code /WX)."""
    tdir = sdir / 'civ5save'
    tdir.mkdir(parents=True, exist_ok=True)
    args = ['-m64', '/c', '/nologo', '/MD', '/GS', '/EHsc', '/Zi', '/FS', '/std:c++14', '/W3', '/WX',
            '-fdiagnostics-absolute-paths', '/D_CRT_SECURE_NO_WARNINGS', f'/I{zsrc}', f'/I{HERE / "src"}']
    args += x64build.repro_cl_args((REPO, 'VPREPO'), (sdir.parent, 'VPOUT'))   # issue 0115
    args += ['/O2', '/DNDEBUG'] if config == 'release' else ['/Od']
    objs = []
    for cpp in (HERE / 'src' / 'srv_civ5save.cpp', HERE / 'src' / 'civ5save-tool' / 'civ5save_main.cpp'):
        obj = tdir / (cpp.stem + '.obj')
        cwd, rel = x64build.source_cwd(cpp, REPO)   # issue 0115: relative source name
        cp = subprocess.run([str(x64build.CLANG_CL)] + args + x64build.objname_args(obj, sdir.parent) +
                            [f'/Fo{obj}', f'/Fd{tdir / "vc.pdb"}', rel],
                            capture_output=True, text=True, errors='replace', env=env, cwd=cwd)
        say(f'compile {cpp.name} (civ5save.exe): rc={cp.returncode}')
        if cp.stdout.strip() or cp.stderr.strip():
            say((cp.stdout + cp.stderr).rstrip(), echo=cp.returncode != 0)
        if cp.returncode != 0:
            fail(f'civ5save.exe: {cpp.name} did not compile')
        objs.append(obj)
    exe = tdir / 'civ5save.exe'
    lp = lambda q: x64build.link_path(q, tdir)   # issue 0115: the PDB records the paths the linker is given
    cmd = [str(x64build.LLD_LINK), f'/OUT:{lp(exe)}', f'/PDB:{lp(exe.with_suffix(".pdb"))}', '/MACHINE:X64',
           '/SUBSYSTEM:CONSOLE', '/DEBUG', '/DYNAMICBASE', '/NXCOMPAT'] + x64build.REPRO_LINK + \
        [lp(o) for o in objs + zobjs] + ['kernel32.lib']
    started = time.time()
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(tdir))
    say(f'link civ5save.exe: rc={cp.returncode}')
    if cp.stdout.strip() or cp.stderr.strip():
        say((cp.stdout + cp.stderr).rstrip())
    why = linkcheck.link_failure(cp.returncode, cp.stdout + cp.stderr, exe, started)
    if why:
        fail(f'civ5save.exe link: {why}')
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', type=Path, default=None, help='DLL fork source at the deps/dll.lock commit')
    ap.add_argument('--out', type=Path, required=True, help='output directory (outside the repository)')
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--deps', type=Path, help='reuse a finished build_all.py --out folder of the same --config')
    ap.add_argument('--offline', action='store_true', help='never download third-party sources')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit (fork development)')
    ap.add_argument('--zlib-tarball', type=Path, help='explicit zlib-1.2.3.tar.gz (checked against deps/third-party.lock)')
    ap.add_argument('--civ5save-only', action='store_true',
                    help='build only zlib and <out>/bin/civ5save.exe (no --src, no DLL)')
    ap.add_argument('--no-proxy', action='store_true', help='do not build the x86 proxy (proxy/build_proxy.py)')
    ap.add_argument('--no-render-events', action='store_true',
                    help='ticket 4932: the DLL without VP64_RENDER_EVENTS (build_all.py --no-render-events; the '
                         'with-and-without identity test only)')
    ap.add_argument('--repo-commit', default=None,
                    help='this repository\'s commit when it is a copy without .git (an archive; issue 2801)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4, help='parallel compiles')
    ap.add_argument('--test-dialog', action='store_true',
                    help='issue 2501: TEST build with /DVP_SERVER_TEST_DIALOG into <out>/bin-testdialog (no proxy)')
    ap.add_argument('--heap-type', choices=['default', 'segment'], default='default',
                    help='ticket 5037 (allocator trial): "segment" embeds an application manifest with '
                         '<heapType>SegmentHeap</heapType>, so the process heap (and the CRT heap of server.exe and '
                         'the DLL, both /MD) is the Segment Heap (Windows 10 2004 or later); "default" (the default) '
                         'links no manifest, as before')
    ap.add_argument('--quietlock-override', metavar='"WHO: WHY"',
                    help='start although another team holds the quiet-machine lock (emergencies only, '
                         'logged; COMMON rule 16)')
    a = ap.parse_args()
    # COMMON rule 16 launch gate (perf/quietlock/gate.py, tracker 7006): refuse while another team holds the
    # quiet-machine lock; the holder's own runs pass (C5_QUIETLOCK_WHO); a gate error never blocks.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'perf' / 'quietlock'))
    try:
        import gate as _launch_gate
    except ImportError:
        _launch_gate = None
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    if _launch_gate:
        _launch_gate.require('a server build (server/build_server.py)', a.quietlock_override)
    out = a.out.resolve()
    try:
        out.relative_to(REPO)
        fail(f'--out {out} is inside the repository')
    except ValueError:
        pass
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.perf_counter()
    if a.civ5save_only:
        sdir = out / 'server'
        sdir.mkdir(parents=True, exist_ok=True)
        tlog = (sdir / 'build_civ5save.log').open('w', encoding='utf-8')

        def tsay(msg, echo=True):
            tlog.write(msg + '\n')
            tlog.flush()
            if echo:
                print(msg, flush=True)
        tenv = x64build.repro_env(x64build.load_vcvars_env(tsay, sdir / 'vcvars-env.json'))   # issue 0115
        try:
            zsrc = unpack_zlib(out, a.zlib_tarball, True if a.offline else None)
        except thirdparty.ThirdPartyError as e:
            fail(str(e))
        tool = build_civ5save_tool(sdir, a.config, tenv, tsay, zsrc, build_zlib(out, a.config, tenv, tsay, zsrc))
        try:
            linkcheck.copy_checked([(tool, True), (tool.with_suffix('.pdb'), False)], out / 'bin', say=tsay)
        except linkcheck.LinkCheckError as e:
            fail(str(e))
        tsay(f'build_server: OK (civ5save only) in {time.perf_counter() - t0:.0f} s: {out / "bin"}')
        return
    try:
        srcinfo = x64build.resolve_src(a.src, a.unlocked)
        a.src = srcinfo['src']
    except x64build.SrcError as e:
        fail(str(e))

    # 1. dependencies
    deps = a.deps.resolve() if a.deps else out / 'deps'
    if not a.deps:
        cmd = [sys.executable, str(X64 / 'build_all.py'), '--src', str(a.src), '--out', str(deps), '--config', a.config]
        if a.offline:
            cmd.append('--offline')
        if a.unlocked:
            cmd.append('--unlocked')
        if a.no_render_events:
            cmd.append('--no-render-events')   # ticket 4932
        print('build_server: ' + ' '.join(cmd), flush=True)
        _rc, _text, why = linkcheck.run_sub_build(cmd, BUILD_ALL_OK, 'tools/x64-dll/build_all.py')
        if why:
            fail(f'{why} (see its last line)')
    summary = deps / 'build_all.json'
    if not summary.exists():
        fail(f'{summary} not found: --deps must be a build_all.py --out folder')
    s = json.loads(summary.read_text(encoding='utf-8'))
    steps = s.get('steps', [])
    bad = [st.get('step') for st in steps if st.get('rc') != 0]
    if bad or len(steps) < 5:
        fail(f'{summary}: build_all did not pass (failed or missing steps: {bad or len(steps)})')
    why = deps_config_problem(s.get('config'), a.config)
    if why:
        fail(f'{summary}: {why}')
    deps_config = s.get('config') or a.config
    if deps_config != a.config:
        print(f'build_server: the DLL of --deps is a {deps_config} build (ticket 0513); the server and the proxy are '
              f'built as {a.config}', flush=True)
    if a.no_render_events and s.get('render_events') == 'on':   # ticket 4932: a --deps of the other arm
        fail(f'{summary}: its DLL has the render messages (render_events on); --no-render-events wants one without')
    if s.get('render_events'):
        print(f'build_server: the DLL\'s render messages (ticket 4932): {s.get("render_events")}', flush=True)
    stubs, dlldir = deps / 'stubs', deps / 'dll'
    for f in (stubs / 'lua51_x64.lib', stubs / 'CvGameDatabase_x64.lib', stubs / 'CvLocalization_x64.lib',
              dlldir / 'CvGameCore_Expansion2.dll'):
        if not f.exists():
            fail(f'{f} not found in --deps')

    # 2. the server
    sdir = out / ('server-testdialog' if a.test_dialog else 'server')
    (sdir / 'obj').mkdir(parents=True, exist_ok=True)
    (sdir / 'gen').mkdir(exist_ok=True)
    log = (sdir / 'build_server.log').open('w', encoding='utf-8')

    def say(msg, echo=True):
        log.write(msg + '\n')
        log.flush()
        if echo:
            print(msg, flush=True)

    # issue 0115: the generator writes the --src folder into a comment; the header's MD5 goes into the PDB (and lld's
    # /Brepro GUID), so the folder is replaced by a fixed name
    (sdir / 'gen' / 'host_ifaces.gen.h').write_text(gen_ifaces.generate(a.src).replace(str(a.src), 'VPSRC'),
                                                    encoding='utf-8')
    # The generated runtime of remote mode (epic 2300): one tree for both sides (proxy/gen/gen_runtime.py).
    gdir = out / 'gen'
    cmd = [sys.executable, str(REPO / 'proxy' / 'gen' / 'gen_runtime.py'), '--out', str(gdir),
           '--hooks-proxy', str(REPO / 'proxy' / 'src'), '--hooks-server', str(HERE / 'src' / 'remote'),
           '--src', str(a.src)]
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    say(' '.join(cmd))
    say((cp.stdout + cp.stderr).rstrip())
    if cp.returncode != 0:
        fail('proxy/gen/gen_runtime.py failed')
    emit_exe_tables(out, say)   # ticket 4960: the EXE address header (local tables, D202)
    env = x64build.repro_env(x64build.load_vcvars_env(say, sdir / 'vcvars-env.json'))   # issue 0115
    try:
        zsrc = unpack_zlib(out, a.zlib_tarball, True if a.offline else None)
    except thirdparty.ThirdPartyError as e:
        fail(str(e))
    zobjs = build_zlib(out, a.config, env, say, zsrc)
    tool = build_civ5save_tool(sdir, a.config, env, say, zsrc, zobjs)
    c5render = build_c5render(out, say)   # issue 3301: the terrain renderer's static library (render/build.py, gated)
    args = ['-m64', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/Zi', '/FS', '/std:c++14',
            '-fdiagnostics-absolute-paths', '/W3', '/WX']
    args += ['/O2'] if a.config == 'release' else ['/Od', '/Oy-']
    # issue 0115: the repository, the fork source and the build folder under fixed names in every compiler string
    args += x64build.repro_cl_args((REPO, 'VPREPO'), (a.src, 'VPSRC'), (out, 'VPOUT'))
    args += [f'/D{d}' for d in x64build.PREDEFS['debug'] if d not in NOT_FOR_HOST and d != 'VPDEBUG']
    args += ['/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING']
    if a.test_dialog:
        args += ['/DVP_SERVER_TEST_DIALOG']   # issue 2501: TEST build only (separate folders, see the docstring)
    args += ['/external:W0'] + [f'/external:I{a.src / d}' for d in x64build.INCLUDE_DIRS]
    args += [f'/I{HERE / "src"}', f'/I{sdir / "gen"}', f'/I{zsrc}']
    args += [f'/I{REPO / "options" / "include"}']  # one-off 9042: the options file (src/srv_options.cpp)
    args += exebuild_includes(out)                 # ticket 4960: the EXE build table
    # issue 2704: the build id (repo commit, dirty, DLL pin) for src/srv_buildid.cpp; server.exe's SHA-256 at run time
    bpath, bvals = build_info.write_header(sdir / 'gen', srcinfo, a.config)
    bvals = _apply_repo_identity(bpath, bvals, REPO, a.repo_commit, say)  # issue 2801: archive copies
    say(f'build id: {bpath}: ' + ', '.join(f'{k[9:].lower()}={v}' for k, v in bvals.items()))
    args += [f'-Wno-{w}' for w in x64build.CL_SUPPRESS] + ['-Wno-deprecated-declarations', '-Wno-microsoft-include',
                                                           '-Wno-pragma-pack', '-Wno-unused-value',
                                                           '-Wno-overloaded-virtual', '-Wno-invalid-offsetof']
    # remote mode: the remote sources and the generated code see the vpipc, wire and generated headers too
    rargs = args + [f'/I{HERE / "src" / "remote"}', f'/I{REPO / "ipc" / "include"}', f'/I{REPO / "proxy" / "include"}',
                    f'/I{gdir / "common"}', f'/I{gdir / "server"}']
    gargs = [x for x in rargs if x != '/WX'] + ['-Wno-unused-variable', '-Wno-unused-function']
    # vpipc (ipc/CMakeLists.txt: W4 /WX, its public defines; C++11 code built as C++14 on x64)
    iargs = ['-m64', '/c', '/MD', '/GS', '/EHsc', '/Zi', '/FS', '/std:c++14', '-fdiagnostics-absolute-paths', '/W4', '/WX',
             '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0601', '/D_CRT_SECURE_NO_WARNINGS',
             f'/I{REPO / "ipc" / "include"}', f'/I{REPO / "ipc" / "src"}']
    iargs += x64build.repro_cl_args((REPO, 'VPREPO'), (out, 'VPOUT'))   # issue 0115
    iargs += ['/O2', '/DNDEBUG'] if a.config == 'release' else ['/Od']
    units = [(cpp, args, 'obj') for cpp in sorted((HERE / 'src').glob('*.cpp'))]
    units += [(cpp, rargs, 'obj/remote') for cpp in sorted((HERE / 'src' / 'remote').glob('*.cpp'))]
    # epic 3300: the renderer's event feed (src/feed/srv_feed.h; included as "feed/srv_feed.h"; no DLL header in it)
    units += [(cpp, args, 'obj/feed') for cpp in sorted((HERE / 'src' / 'feed').glob('*.cpp'))]
    # epic 3300, issue 3301: the renderer inside server.exe (src/render/, the ring sink of shell/frame/). Compiled WITHOUT
    # the DLL fork's include path and without the DLL's predefines (render_compile_args): the render thread cannot name a
    # DLL type; an include of a DLL-facing header (srv_common.h, srv_remote.h, a fork header) fails to compile.
    nargs = render_compile_args(a.config, out)
    if render_args_see_dll(nargs, a.src):
        fail('internal: the render units would see the DLL fork source')
    (sdir / 'render-compile-args.json').write_text(json.dumps(nargs, indent=1), encoding='utf-8')   # for the tests
    units += [(cpp, nargs, 'obj/render') for cpp in render_sources()]
    units += [(cpp, gargs, 'obj/gen') for cpp in sorted((gdir / 'server').glob('*.cpp'))]
    units += [(gdir / 'common' / 'vp_ids.gen.cpp', gargs, 'obj/gen')]
    units += [(cpp, iargs, 'obj/vpipc') for cpp in sorted((REPO / 'ipc' / 'src').glob('*.cpp'))]
    # one-off 9042: the options file loader shared with the proxy (options/CMakeLists.txt flags: W4 /WX, its defines)
    oargs = [x for x in iargs if not x.startswith('/I')] + [f'/I{REPO / "options" / "include"}']
    units += [(cpp, oargs, 'obj/options') for cpp in sorted((REPO / 'options' / 'src').glob('*.cpp'))]

    def compile_one(unit):
        cpp, cargs, sub = unit
        odir = sdir / sub
        odir.mkdir(parents=True, exist_ok=True)
        obj = odir / (cpp.stem + '.obj')
        cwd, rel = x64build.source_cwd(cpp, REPO, out)   # issue 0115: relative source name
        cmd = [str(x64build.CLANG_CL)] + cargs + x64build.objname_args(obj, out) + \
            [f'/Fo{obj}', f'/Fd{odir / "vc.pdb"}', rel]
        cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=cwd)
        return cpp, obj, cp

    objs, ok = [], True
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        for cpp, obj, cp in ex.map(compile_one, units):
            say(f'compile {cpp.name}: rc={cp.returncode}')
            if cp.stdout.strip() or cp.stderr.strip():
                say((cp.stdout + cp.stderr).rstrip(), echo=cp.returncode != 0)
            ok &= cp.returncode == 0
            objs.append(obj)
    if not ok:
        fail(f'compile errors (warnings are errors, /WX); see {sdir / "build_server.log"}')
    exe = sdir / 'server.exe'
    lp = lambda q: x64build.link_path(q, sdir)   # issue 0115: the PDB records the paths the linker is given
    lines = [f'/OUT:"{lp(exe)}"', f'/PDB:"{lp(exe.with_suffix(".pdb"))}"', f'/MAP:"{lp(exe.with_suffix(".map"))}"',
             '/MACHINE:X64', '/SUBSYSTEM:CONSOLE', '/DEBUG', '/DYNAMICBASE', '/NXCOMPAT', '/ERRORLIMIT:0',
             '/STACK:16777216', f'"/EXPORT:{INIT_LOCKS_X64}=HostInitLocksAssign"', '/DELAYLOAD:lua51_x64.dll',
             '/DELAYLOAD:CvGameDatabase_x64.dll', '/DELAYLOAD:CvLocalization_x64.dll'] + \
        [f'/DELAYLOAD:{d}' for d in RENDER_DELAYLOAD] + x64build.REPRO_LINK
    lines += [f'"{lp(o)}"' for o in objs + zobjs]
    # FireWorks.lib (closed-library replacement, issue 1006): the server serialises through the same FMemoryStream /
    # FDataStream code the DLL links (issue 1506).
    lines += [f'"{lp(stubs / "lua51_x64.lib")}"', f'"{lp(stubs / "CvGameDatabase_x64.lib")}"',
              f'"{lp(stubs / "CvLocalization_x64.lib")}"', f'"{lp(stubs / "FireWorks.lib")}"',
              'delayimp.lib', 'kernel32.lib',
              'user32.lib', 'advapi32.lib', 'ole32.lib', 'psapi.lib']
    # issue 3301: c5render and the D3D libraries (render/README.md); d3d11, dxgi and d3dcompiler are delay-loaded, so a
    # server with render=0 never loads them (srv_remote_main.cpp binds them up front when the renderer starts, issue 9008)
    lines += [f'"{lp(c5render)}"', f'"{lp(c5r_models_lib(c5render))}"'] + RENDER_LIBS   # epic 0375, issue 4454
    lines += heap_manifest_link_lines(a.heap_type, sdir, lp, say)   # ticket 5037: the Segment Heap trial
    rsp = sdir / 'link_server.rsp'
    rsp.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    started = time.time()
    cp = subprocess.run([str(x64build.LLD_LINK), f'@{rsp}'], capture_output=True, text=True, errors='replace', env=env,
                        cwd=str(sdir))
    say(f'link: rc={cp.returncode}')
    if cp.stdout.strip() or cp.stderr.strip():
        say((cp.stdout + cp.stderr).rstrip())
    why = linkcheck.link_failure(cp.returncode, cp.stdout + cp.stderr, exe, started)
    if why:
        fail(f'link: {why}; see {sdir / "build_server.log"}')

    # 3. bin
    bin_ = out / ('bin-testdialog' if a.test_dialog else 'bin')
    copied = copy_bin(bin_, exe, tool, dlldir, stubs, say)
    (bin_ / 'build.json').write_text(json.dumps({'src': str(a.src), 'config': a.config, 'deps': str(deps),
                                                 'deps_config': deps_config,   # ticket 0513: release-pgo's DLL
                                                 'test_dialog': bool(a.test_dialog), 'heap_type': a.heap_type,
                                                 'seconds': round(time.perf_counter() - t0, 1),
                                                 'files': copied}, indent=1),
                                     encoding='utf-8')
    say(f'build_server: server OK in {time.perf_counter() - t0:.0f} s: {bin_}')

    # 4. the x86 proxy (task 2302) from the same generated tree (not for the issue 2501 test build)
    if a.test_dialog:
        say(f'build_server: TEST build (VP_SERVER_TEST_DIALOG) OK in {time.perf_counter() - t0:.0f} s: {bin_}')
        return
    build_proxy_step(REPO / 'proxy' / 'build_proxy.py', a.no_proxy, out, a.src, gdir, a.config, say, a.repo_commit,
                     unlocked=a.unlocked)
    say(f'build_server: OK in {time.perf_counter() - t0:.0f} s: {bin_}')


# Ticket 5037: the application manifest of the Segment Heap trial. Only the heap type; no UAC element (/MANIFESTUAC:NO)
# and no compatibility section, so nothing else about the process changes against a server.exe without a manifest.
SEGMENT_HEAP_MANIFEST = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <heapType xmlns="http://schemas.microsoft.com/SMI/2020/WindowsSettings">SegmentHeap</heapType>
    </windowsSettings>
  </application>
</assembly>
'''


def heap_manifest_link_lines(heap_type, sdir, lp, say=print):
    """Ticket 5037: the link lines for --heap-type. 'default' adds nothing (server.exe has no manifest, as before);
    'segment' writes <sdir>/server.heap.manifest and embeds it (lld-link /MANIFEST:EMBED /MANIFESTINPUT)."""
    if heap_type == 'default':
        return []
    if heap_type != 'segment':
        fail(f'internal: unknown heap type {heap_type!r}')
    man = sdir / 'server.heap.manifest'
    man.write_text(SEGMENT_HEAP_MANIFEST, encoding='utf-8')
    say(f'heap type: segment (manifest {man} embedded)')
    return ['/MANIFEST:EMBED', '/MANIFESTUAC:NO', f'"/MANIFESTINPUT:{lp(man)}"']


def copy_bin(bin_, exe, tool, dlldir, stubs, say=print):
    """Step 3 (one-off 9023, P10): the binaries the server runs with, into bin_. Every .exe and .dll is required (a
    missing one fails the build and nothing is copied); a missing .pdb removes the earlier build's copy. One line per
    file with SHA-256, size and timestamp; returns their records for build.json."""
    files = []
    for f in [exe, tool, dlldir / 'CvGameCore_Expansion2.dll', stubs / 'lua51_x64.dll', stubs / 'CvGameDatabase_x64.dll',
              stubs / 'CvLocalization_x64.dll']:
        files += [(f, True), (f.with_suffix('.pdb'), False)]
    try:
        return linkcheck.copy_checked(files, bin_, say=say)
    except linkcheck.LinkCheckError as e:
        fail(str(e))


# Ticket 0513 (D184): the DLL configurations a --deps folder may hold for each --config of this build. The server and
# the proxy have no PGO build of their own; a release-pgo DLL (tools/x64-dll/build_all.py --config release-pgo) is laid
# out by a release build of them, and bin/build.json records `deps_config`.
DEPS_CONFIGS = {'debug': ('debug',), 'release': ('release', 'release-pgo')}


def deps_config_problem(deps_config, config) -> str | None:
    """None when a build_all.py output of `deps_config` (None: an old record without one) may be used by a build of
    `config`, else the reason."""
    if not deps_config or deps_config in DEPS_CONFIGS.get(config, (config,)):
        return None
    return f'config {deps_config} differs from --config {config}' + (
        ' (a release-pgo DLL is laid out by --config release only)' if deps_config == 'release-pgo' else '')


def build_proxy_step(bp, no_proxy, out, src, gdir, config, say=print, repo_commit=None, unlocked=False):
    """Step 4. A missing proxy/build_proxy.py is a failure (issue 9008: it used to end OK with a note), unless
    --no-proxy was given. `unlocked` passes --unlocked on, so a server built from a fork commit other than the
    deps/dll.lock pin also gets its proxy (issue 9029; found by one-off 9020, whose proxy step failed on the lock)."""
    if no_proxy:
        say('build_server: proxy skipped (--no-proxy)')
        return
    if not bp.exists():
        fail(f'{bp} does not exist: the proxy cannot be built (--no-proxy builds the server alone)')
    cmd = [sys.executable, str(bp), '--out', str(out / 'proxy'), '--src', str(src), '--gen', str(gdir),
           '--config', config] + (['--repo-commit', repo_commit] if repo_commit else []) + \
          (['--unlocked'] if unlocked else [])
    say('build_server: ' + ' '.join(cmd))
    _rc, _text, why = linkcheck.run_sub_build(cmd, BUILD_PROXY_OK, 'proxy/build_proxy.py')
    if why:
        fail(f'{why} (see its output; --no-proxy builds the server alone)')
    say(f'build_server: proxy built into {out / "proxy" / "bin"}')


if __name__ == '__main__':
    main()
