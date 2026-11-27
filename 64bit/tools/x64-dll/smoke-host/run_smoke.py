"""Run the x64 smoke host against a built DLL (issue 1007).

Copies the DLL and its PDB from --dll-dir, and every other *.dll with its PDB from each --dep-dir (the stubs
directory: lua51_x64.dll, CvGameDatabase_x64.dll, ...; default --dll-dir; issue 1405), into <run>/dll (the build
directories stay untouched and the DLL is never loaded from another worker's directory). The host loads the DLL
with LOAD_WITH_ALTERED_SEARCH_PATH, so its imports resolve from <run>/dll. Makes a fresh <run>/work, runs
smoke_host.exe there with a timeout, and prints the step table of the report. Nothing is written outside --run.

Usage: python run_smoke.py --host <dir with smoke_host.exe> --dll-dir <dir with CvGameCore_Expansion2.dll> --run <dir>
       [--dep-dir <dir with the DLL's dependency DLLs> ...] [--crash-to-dll-filter] [--timeout S]
       (--lua-dir DIR is the older name of --dep-dir and still works)
Exit code: the host's (0 all steps passed, 2 a step raised an exception), 3 timeout, 4 setup error.
"""
import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

DLL = 'CvGameCore_Expansion2'


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


SYMBOLIZER = Path(r'C:\Program Files\LLVM\bin\llvm-symbolizer.exe')
ADDR_RE = re.compile(r'(?P<mod>[\w.-]+\.(?:dll|exe))\+0x(?P<rva>[0-9a-fA-F]+)')


def symbolize(text: str, modules: dict) -> str:
    """Append `function [file:line]` after every `<module>+0x<rva>` of a module with a PDB (llvm-symbolizer)."""
    wanted = {}
    for m in ADDR_RE.finditer(text):
        if m.group('mod').lower() in modules:
            wanted.setdefault(m.group('mod').lower(), set()).add(int(m.group('rva'), 16))
    names = {}
    for mod, rvas in wanted.items():
        rvas = sorted(rvas)
        cp = subprocess.run([str(SYMBOLIZER), f'--obj={modules[mod]}', '--relative-address', '--functions=linkage',
                             '--demangle'] + [hex(r) for r in rvas], capture_output=True, text=True, errors='replace')
        blocks = [b.splitlines() for b in cp.stdout.strip().split('\n\n')]
        for rva, lines in zip(rvas, blocks):
            if len(lines) >= 2 and lines[0] != '??':
                loc = lines[1]
                i = loc.find('Community-Patch-DLL')
                if i < 0:
                    i = loc.find('lua51-src')
                if i >= 0:
                    loc = loc[i:]
                names[(mod, rva)] = f'{lines[0]} [{loc}]'

    def repl(m):
        key = (m.group('mod').lower(), int(m.group('rva'), 16))
        return m.group(0) + (' ' + names[key] if key in names else '')
    return ADDR_RE.sub(repl, text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', type=Path, required=True)
    ap.add_argument('--dll-dir', type=Path, required=True)
    ap.add_argument('--run', type=Path, required=True)
    ap.add_argument('--dep-dir', '--lua-dir', dest='dep_dirs', type=Path, action='append',
                    help='directory whose *.dll (with PDBs) go next to the DLL: the stubs dir with lua51_x64.dll, '
                         'CvGameDatabase_x64.dll; repeatable; default --dll-dir')
    ap.add_argument('--db', type=Path,
                    help='gameplay database (e.g. a copy of Civ5DebugDatabase.db); copied into <run>/db/ and opened '
                         'by the host through CvGameDatabase_x64.dll (issue 1404). Without it the host passes a zeroed '
                         'block, which only the logging stubs of epic 1000 survive')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--crash-to-dll-filter', action='store_true',
                    help="pass the first step exception to the DLL's unhandled-exception filter")
    a = ap.parse_args()
    run = a.run.resolve()
    dll_dir = run / 'dll'
    work = run / 'work'
    dll_dir.mkdir(parents=True, exist_ok=True)
    if work.exists():
        shutil.rmtree(work)
    for old in dll_dir.glob('*'):  # nothing left over from an earlier run with other dependencies
        if old.is_file():
            old.unlink()
    if not (a.dll_dir / (DLL + '.dll')).exists():
        print(f'run_smoke: FAILED: {a.dll_dir / (DLL + ".dll")} does not exist')
        sys.exit(4)
    for ext in ('.dll', '.pdb'):
        src = a.dll_dir / (DLL + ext)
        if src.exists():
            shutil.copy2(src, dll_dir / src.name)
    deps = []
    for d in a.dep_dirs or [a.dll_dir]:
        for src in sorted(d.glob('*.dll')):
            if src.stem.lower() == DLL.lower():
                continue
            shutil.copy2(src, dll_dir / src.name)
            if src.with_suffix('.pdb').exists():
                shutil.copy2(src.with_suffix('.pdb'), dll_dir / src.with_suffix('.pdb').name)
            deps.append(dll_dir / src.name)
    dll = dll_dir / (DLL + '.dll')
    for dep in deps:
        print(f'dependency {dep} sha256 {sha256(dep)}')
    print(f'DLL {dll} sha256 {sha256(dll)}')
    cmd = [str(a.host.resolve() / 'smoke_host.exe'), '--dll', str(dll), '--workdir', str(work)]
    if a.db:
        if not a.db.is_file():
            print(f'run_smoke: FAILED: --db {a.db} does not exist')
            sys.exit(4)
        dbdir = run / 'db'
        if dbdir.exists():
            shutil.rmtree(dbdir)
        dbdir.mkdir()
        db = dbdir / a.db.name
        shutil.copyfile(a.db, db)  # a fresh writable copy per run: post-processing and Analyze() write into it
        print(f'database {a.db} sha256 {sha256(a.db)} -> {db}')
        cmd += ['--db', str(db)]
    if a.crash_to_dll_filter:
        cmd.append('--crash-to-dll-filter')
    with (run / 'stdout.txt').open('w', encoding='utf-8', errors='replace') as out:
        try:
            rc = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, timeout=a.timeout,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).returncode
        except subprocess.TimeoutExpired:
            rc = 3
    report = work / 'smoke-report.md'
    print(f'exit {rc}; report {report}')
    if report.exists():
        modules = {'cvgamecore_expansion2.dll': dll, 'smoke_host.exe': a.host.resolve() / 'smoke_host.exe'}
        modules.update({dep.name.lower(): dep for dep in deps})
        text = symbolize(report.read_text(encoding='utf-8', errors='replace'), modules)
        report.write_text(text, encoding='utf-8')
        steps = text.split('## Steps', 1)[-1].split('## Exceptions', 1)[0]
        print(steps.strip())
    sys.exit(rc)


if __name__ == '__main__':
    main()
