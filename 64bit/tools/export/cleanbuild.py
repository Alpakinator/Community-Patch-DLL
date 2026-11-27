"""The clean build: the release set of the 64-bit patch in one command (plan 4.6, package E5, ticket 4706).

Run it in `64bit/` of an upstream-layout checkout (the DLL repository with this folder inside it, so the DLL source is
`..`, where `../CvGameCoreDLL_Expansion2` is), or in this repository with --src naming a DLL source:

    python -I tools/export/cleanbuild.py --out <folder> --exe <CivilizationV_DX11.exe of build 4390913>
                                         --exe <CivilizationV_DX11.exe of build 26577625>
                                         --db <Civ5DebugDatabase.db> [--offline]

BUILDING.md (the repository root) lists the prerequisites. Steps, each logged to <out>/cleanbuild.log:

1. prerequisites  the toolchain files the builders use, at the folders they expect (LLVM 19.1.7, VS 2022 with MSVC
                  14.4x, the Windows SDK 10.0.26100.0, Ninja and CMake, VC++ 2008 with SDK v6.0A, Python 3.11+);
                  every missing one is named, then the build stops.
2. EXE tables     the per-build EXE address tables (D202) from the builder's own EXE files: one --exe per build that
                  exebuild/include/vp_exe_build.h knows, the anchor build 4390913 among them (tools/exebuild/
                  gen_table.py --identity, then one carry per other build), into <out>/exe-tables. Nothing of the EXE
                  is in this source; the tables stay in <out>.
3. server         server/build_server.py --config release --src <src> --out <out>/build: the x64 DLL and its three
                  libraries (tools/x64-dll/build_all.py), server.exe, civ5save.exe, the x86 proxy DLL and
                  vp64helper.exe (proxy/build_proxy.py; the proxy compiles against the same DLL source's headers with
                  the VC9 toolchain, so no second, 32-bit DLL source is needed).
4. composite      shell/build.cmd with C5_SHELL_BUILD=<out>/shell: shell_composite.dll (--no-composite leaves it out).
5. layout         <out>/release: bin/ (server.exe, civ5save.exe, the x64 DLL, lua51_x64.dll, CvGameDatabase_x64.dll,
                  CvLocalization_x64.dll, their PDBs, build.json), proxy/bin/ (the proxy DLL, vp64helper.exe, PDBs,
                  shell_composite.dll with its .sha256, .ini and MinHook's licence), proxy/build-proxy.json, and
                  SHA256.txt over every .dll and .exe: the file set of the release layout
                  (tools/release/stages/build.py layout_x64).

The third-party archives of deps/third-party.lock (Lua 5.1.4, SQLite 3.7.17, zlib 1.2.3) are read from
--third-party (default <out>/third-party), checked by SHA-256, and downloaded from the lock's URL into it only
without --offline. Every child gets C5_EXE_TABLES, CV64_DEPS_CACHE, C5_PURITY_TEST_CACHE and VP_PROXY_GEN_WORK under
<out>, and C5_SETTINGS_FILE naming an absent file, so no machine setting and no shared cache folder is read or written.
Outputs only under --out, which must lie outside this folder and the DLL source. Exit 0 with a last line
`cleanbuild: OK <out>/release`, 1 with `cleanbuild: FAILED: <reason>`, 2 for a usage error.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]   # the folder that holds tools/: `64bit/` in the upstream layout

DLL = 'CvGameCore_Expansion2.dll'
BIN_FILES = ('server.exe', 'civ5save.exe', DLL, 'lua51_x64.dll', 'CvGameDatabase_x64.dll', 'CvLocalization_x64.dll')
PROXY_FILES = (DLL, 'vp64helper.exe')
COMPOSITE = 'shell_composite.dll'
COMPOSITE_PDB = 'shell_composite.pdb'
COMPOSITE_SHA = 'shell_composite.sha256'
COMPOSITE_INI = 'shell_composite.ini'
MINHOOK_NOTICE = 'MinHook-LICENSE.txt'
LLVM_VERSION = '19.1.7'


class Failed(Exception):
    pass


def _module(name, path: Path):
    spec = importlib.util.spec_from_file_location(name, str(path))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def builds_module():
    return _module('cleanbuild_builds', ROOT / 'tools' / 'exebuild' / 'builds.py')


def toolchain_paths() -> list:
    """(what, path) of every toolchain file the builders open at a fixed folder (tools/x64-dll/build.py,
    proxy/gen/toolchain.py, proxy/build_proxy.py, render/build.py, models/build.py, shell/build.cmd)."""
    llvm = Path(r'C:\Program Files\LLVM\bin')
    vs = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community')
    vc9 = Path(r'C:\Program Files (x86)\Microsoft Visual Studio 9.0')
    sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
    return [
        ('LLVM clang-cl', llvm / 'clang-cl.exe'),
        ('LLVM lld-link', llvm / 'lld-link.exe'),
        ('LLVM llvm-lib', llvm / 'llvm-lib.exe'),
        ('LLVM llvm-readobj', llvm / 'llvm-readobj.exe'),
        ('VS 2022 vcvars64.bat', vs / 'VC' / 'Auxiliary' / 'Build' / 'vcvars64.bat'),
        ('VS 2022 vcvarsall.bat', vs / 'VC' / 'Auxiliary' / 'Build' / 'vcvarsall.bat'),
        ('VS 2022 Ninja (C++ CMake tools)', vs / 'Common7' / 'IDE' / 'CommonExtensions' / 'Microsoft' / 'CMake' /
         'Ninja' / 'ninja.exe'),
        ('VS 2022 CMake (C++ CMake tools)', vs / 'Common7' / 'IDE' / 'CommonExtensions' / 'Microsoft' / 'CMake' /
         'CMake' / 'bin' / 'cmake.exe'),
        ('Windows SDK 10.0.26100.0 headers', sdk / 'Include' / '10.0.26100.0' / 'um' / 'windows.h'),
        ('Windows SDK 10.0.26100.0 fxc', sdk / 'bin' / '10.0.26100.0' / 'x64' / 'fxc.exe'),
        ('VC++ 2008 vsvars32.bat', vc9 / 'Common7' / 'Tools' / 'vsvars32.bat'),
        ('VC++ 2008 compiler (VC9)', vc9 / 'VC' / 'bin' / 'cl.exe'),
        ('Windows SDK v6.0A mt.exe (with VC++ 2008)', Path(r'C:\Program Files\Microsoft SDKs\Windows\v6.0A\bin\mt.exe')),
    ]


def llvm_version(clang: Path):
    try:
        cp = subprocess.run([str(clang), '--version'], capture_output=True, text=True, errors='replace', timeout=60)
    except OSError:
        return None
    m = re.search(r'clang version (\d+\.\d+\.\d+)', cp.stdout)
    return m.group(1) if m else None


def check_prerequisites(paths=None, version_of=llvm_version, python=sys.version_info) -> list:
    """Problems (one line each) with the toolchain; [] when everything the builders open is there."""
    probs = []
    if tuple(python[:2]) < (3, 11):
        probs.append(f'Python 3.11 or later is needed (this is {python[0]}.{python[1]})')
    paths = toolchain_paths() if paths is None else paths
    for what, p in paths:
        if not Path(p).is_file():
            probs.append(f'{what}: {p} not found')
    clang = next((p for w, p in paths if w == 'LLVM clang-cl'), None)
    if clang is not None and Path(clang).is_file():
        v = version_of(Path(clang))
        if v != LLVM_VERSION:
            probs.append(f'LLVM {LLVM_VERSION} is needed ({clang} is {v or "of an unknown version"})')
    return probs


def check_layout(src: Path, out: Path) -> list:
    probs = []
    if not (src / 'CvGameCoreDLL_Expansion2').is_dir() or not (src / 'build_vp_clang.py').is_file():
        probs.append(f'--src {src}: not a DLL source (no CvGameCoreDLL_Expansion2/ or build_vp_clang.py); in the '
                     f'upstream layout run this in 64bit/ of the DLL repository')
    for name, tree in (('this folder', ROOT), ('the DLL source', src)):
        if _inside(out, tree):
            probs.append(f'--out {out} lies inside {name} ({tree}); give a folder outside both')
    return probs


def _inside(p: Path, tree: Path) -> bool:
    try:
        p.relative_to(tree)
        return True
    except ValueError:
        return False


def identify_exes(exes, builds=None) -> dict:
    """{steam build: path} for the --exe files; Failed names an unknown file, a duplicate or a missing known build."""
    builds = builds or builds_module()
    got = {}
    for p in exes:
        b = builds.of_file(p)
        if b is None:
            raise Failed(f'--exe {p}: not a DX11 EXE of a build the patch knows '
                         f'({", ".join(k.steam_build for k in builds.KNOWN)}; tools/exebuild/builds.py)')
        if b.steam_build in got:
            raise Failed(f'--exe {p}: build {b.steam_build} given twice ({got[b.steam_build]})')
        got[b.steam_build] = Path(p)
    missing = [k.steam_build for k in builds.KNOWN if k.steam_build not in got]
    if missing:
        raise Failed('the build needs an address table for every known EXE build; no --exe for build(s) '
                     + ', '.join(missing) + f' (the anchor build is {builds.ANCHOR.steam_build})')
    return got


def child_env(out: Path, third_party: Path, base=None) -> dict:
    env = dict(os.environ if base is None else base)
    env.update({
        'C5_EXE_TABLES': str(out / 'exe-tables'),
        'CV64_DEPS_CACHE': str(third_party),
        'C5_PURITY_TEST_CACHE': str(out / 'cache' / 'purity-test'),
        'VP_PROXY_GEN_WORK': str(out / 'cache' / 'proxy-gen'),
        'C5_SETTINGS_FILE': str(out / 'no-local-settings.ini'),   # absent on purpose: no machine setting is read
        'VSCMD_SKIP_SENDTELEMETRY': '1',
        'PYTHONUTF8': '1',
    })
    for k in ('VP_DLL_SRC', 'VP_DLL_FORK', 'VP_PROXY_GEN_SRC', 'VP_PROXY_GEN_COMMIT', 'C5_SHELL_BUILD'):
        env.pop(k, None)
    return env


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest().upper()


def layout(build: Path, shell, dst: Path, say=print) -> list:
    """The release set under dst from <build> (build_server.py's --out) and shell (shell/build.cmd's output or None).
    Returns the relative paths SHA256.txt names. Failed when a file of the set is missing."""
    if dst.exists():
        shutil.rmtree(dst)
    (dst / 'bin').mkdir(parents=True)
    (dst / 'proxy' / 'bin').mkdir(parents=True)
    rels = []
    for name in BIN_FILES:
        f = build / 'bin' / name
        if not f.is_file():
            raise Failed(f'{f} missing after the build')
    for f in sorted((build / 'bin').iterdir()):
        if f.is_file():
            shutil.copy2(f, dst / 'bin' / f.name)
            if f.suffix.lower() in ('.dll', '.exe'):
                rels.append(f'bin/{f.name}')
    pbin = build / 'proxy' / 'bin'
    for name in PROXY_FILES:
        if not (pbin / name).is_file():
            raise Failed(f'{pbin / name} missing after the build')
    for f in sorted(pbin.iterdir()):
        if f.is_file():
            shutil.copy2(f, dst / 'proxy' / 'bin' / f.name)
            if f.suffix.lower() in ('.dll', '.exe'):
                rels.append(f'proxy/bin/{f.name}')
    bp = build / 'proxy' / 'build-proxy.json'
    if bp.is_file():
        shutil.copy2(bp, dst / 'proxy' / 'build-proxy.json')
    if shell is not None:
        comp = Path(shell) / 'bin' / COMPOSITE
        if not comp.is_file():
            raise Failed(f'{comp} missing after shell/build.cmd')
        pb = dst / 'proxy' / 'bin'
        shutil.copy2(comp, pb / COMPOSITE)
        if (comp.parent / COMPOSITE_PDB).is_file():
            shutil.copy2(comp.parent / COMPOSITE_PDB, pb / COMPOSITE_PDB)
        sm = _module('cleanbuild_shell_manifest', ROOT / 'proxy' / 'tools' / 'shell_manifest.py')
        text = sm.manifest_text(sha256_file(pb / DLL).lower(), sha256_file(pb / COMPOSITE).lower(),
                                'tools/export/cleanbuild.py')
        (pb / COMPOSITE_SHA).write_bytes(text.encode('ascii'))
        shutil.copyfile(ROOT / 'shell' / 'composite' / COMPOSITE_INI, pb / COMPOSITE_INI)
        shutil.copyfile(ROOT / 'shell' / 'third_party' / 'minhook' / 'LICENSE.txt', pb / MINHOOK_NOTICE)
        rels.append(f'proxy/bin/{COMPOSITE}')
        rels += [f'proxy/bin/{COMPOSITE_SHA}', f'proxy/bin/{COMPOSITE_INI}', f'proxy/bin/{MINHOOK_NOTICE}']
    rels = sorted(set(rels))
    # the format of tools/release/stages/_common.py write_sha_file
    (dst / 'SHA256.txt').write_text(''.join(f'{sha256_file(dst / r)}  {r}  {(dst / r).stat().st_size} B\n'
                                            for r in rels), encoding='utf-8')
    say(f'layout: {len(rels)} files in {dst}')
    return rels


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True, help='output folder, outside this folder and the DLL source')
    ap.add_argument('--exe', type=Path, action='append', default=[],
                    help='CivilizationV_DX11.exe of one known build (repeat: every known build, the anchor 4390913 '
                         'among them); only read')
    ap.add_argument('--db', type=Path, required=True,
                    help="the game's Civ5DebugDatabase.db with Vox Populi active, for the DLL's smoke run "
                         '(tools/x64-dll/build_all.py --db; only read)')
    ap.add_argument('--hand-review', action='append', default=[], metavar='BUILD=FILE',
                    help="answers for the symbols the carry to BUILD cannot match by itself (gen_table.py's "
                         'hand-review.tsv; local only, never in this source); without them that carry may refuse')
    ap.add_argument('--src', type=Path, default=ROOT.parent,
                    help='the DLL source (default: .. , the upstream layout)')
    ap.add_argument('--third-party', type=Path, help='folder of the deps/third-party.lock archives '
                                                     '(default <out>/third-party)')
    ap.add_argument('--offline', action='store_true', help='never download a third-party archive')
    ap.add_argument('--no-composite', action='store_true', help='leave shell_composite.dll out')
    ap.add_argument('--check-only', action='store_true', help='steps 1 and 2 only')
    ap.add_argument('--repo-commit', help="this folder's commit when it has no .git (server/build_server.py)")
    ap.add_argument('--unlocked', action='store_true', help='a DLL source at another commit than deps/dll.lock')
    a = ap.parse_args(argv)
    out = a.out.resolve()
    src = a.src.resolve()
    if not a.exe:
        ap.error('--exe is required (one per known EXE build)')
    probs = check_layout(src, out)
    if probs:
        for p in probs:
            print(f'cleanbuild: {p}', flush=True)
        print('cleanbuild: FAILED: usage', flush=True)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    tp = (a.third_party or out / 'third-party').resolve()
    tp.mkdir(parents=True, exist_ok=True)
    log = open(out / 'cleanbuild.log', 'a', encoding='utf-8')
    t0 = time.time()

    def say(msg):
        line = f'cleanbuild: {msg}'
        print(line, flush=True)
        log.write(f'{time.strftime("%Y-%m-%d %H:%M:%S")} {line}\n')
        log.flush()

    def run(step, cmd, env, cwd=ROOT):
        say(f'{step}: ' + ' '.join(str(c) for c in cmd))
        s = time.time()
        with open(out / f'{step}.log', 'w', encoding='utf-8', errors='replace') as f:
            rc = subprocess.run([str(c) for c in cmd], cwd=str(cwd), env=env, stdout=f, stderr=subprocess.STDOUT).returncode
        tail = (out / f'{step}.log').read_text(encoding='utf-8', errors='replace').strip().splitlines()[-1:] or ['']
        say(f'{step}: exit {rc} in {time.time() - s:.0f} s; last line: {tail[0][:300]}')
        return rc

    try:
        say(f'source {src}; this folder {ROOT}; out {out}; third-party {tp}{" (offline)" if a.offline else ""}')
        probs = check_prerequisites()
        if probs:
            raise Failed('prerequisites missing (BUILDING.md): ' + '; '.join(probs))
        say(f'prerequisites: {len(toolchain_paths())} toolchain files found, LLVM {LLVM_VERSION}, Python '
            f'{sys.version.split()[0]}')
        exes = identify_exes(a.exe)
        if not a.db.is_file():
            raise Failed(f'--db {a.db}: no such file')
        env = child_env(out, tp)
        env['C5_RELEASE_TEST_DB'] = str(a.db.resolve())   # build_all.py's smoke database (default_db)
        if a.offline:
            env['CV64_OFFLINE'] = '1'
        b = builds_module()
        anchor = exes[b.ANCHOR.steam_build]
        gen = ROOT / 'tools' / 'exebuild' / 'gen_table.py'
        if run('tables-' + b.ANCHOR.steam_build, [sys.executable, '-I', gen, '--identity', '--old', anchor], env):
            raise Failed(f'gen_table.py --identity (see {out / ("tables-" + b.ANCHOR.steam_build + ".log")})')
        for spec in a.hand_review:
            sb, sep, f = spec.partition('=')
            if not sep or sb not in exes or sb == b.ANCHOR.steam_build or not Path(f).is_file():
                raise Failed(f'--hand-review {spec}: not BUILD=FILE for a carried build given by --exe')
            dst = out / 'exe-tables' / sb / 'hand-review.tsv'
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(f, dst)
            say(f'hand review for build {sb}: {f} (SHA-256 {sha256_file(dst)})')
        incomplete = []
        for sb, exe in sorted(exes.items()):
            if sb == b.ANCHOR.steam_build:
                continue
            rc = run(f'tables-{sb}', [sys.executable, '-I', gen, '--old', anchor, '--new', exe, '--steam-build', sb],
                     env)
            if rc == 4:
                incomplete.append(sb)
            elif rc:
                raise Failed(f'gen_table.py for build {sb} exit {rc} (see {out / f"tables-{sb}.log"}; exit 1 for BAD '
                             f'patterns means symbols the matcher cannot carry: answer them with --hand-review '
                             f'{sb}=<hand-review.tsv>, BUILDING.md)')
        if incomplete:
            say('WARNING: the table of build(s) ' + ', '.join(incomplete) + ' is incomplete (gen_table exit 4): '
                'the symbols it lists in <out>/exe-tables/<build>/review.tsv need answers in hand-review.tsv; the '
                'build goes on, and the product treats such a build as not supported where it needs those symbols')
        if a.check_only:
            say(f'OK (check only) in {time.time() - t0:.0f} s')
            return 0
        cmd = [sys.executable, '-I', '-X', 'utf8', ROOT / 'server' / 'build_server.py', '--config', 'release',
               '--src', src, '--out', out / 'build'] + (['--offline'] if a.offline else []) + \
              (['--repo-commit', a.repo_commit] if a.repo_commit else []) + (['--unlocked'] if a.unlocked else [])
        if run('server', cmd, env):
            raise Failed(f'server/build_server.py (see {out / "server.log"})')
        shell = None
        if not a.no_composite:
            senv = dict(env, C5_SHELL_BUILD=str(out / 'shell'))
            if run('shell', ['cmd', '/d', '/c', ROOT / 'shell' / 'build.cmd'], senv):
                raise Failed(f'shell/build.cmd (see {out / "shell.log"})')
            shell = out / 'shell'
        rels = layout(out / 'build', shell, out / 'release', say)
        (out / 'cleanbuild.json').write_text(json.dumps({
            'src': str(src), 'root': str(ROOT), 'exe_builds': sorted(exes), 'tables_incomplete': incomplete,
            'files': rels, 'seconds': round(time.time() - t0)}, indent=1), encoding='utf-8')
        say(f'OK {out / "release"} in {time.time() - t0:.0f} s')
        return 0
    except Failed as e:
        say(f'FAILED: {e}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
