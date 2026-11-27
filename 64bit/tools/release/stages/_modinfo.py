"""The VFS imports of a mod's .modinfo against its .civ5proj, and the fix ModBuddy's release build makes (issue 0310).

Upstream master's committed `(1) Community Patch (v 151).modinfo` (ba8aafdca, queue ticket 3515) lacks four files that
`Community Patch.civ5proj` imports into the VFS (Gamecore/GamecoreInit.lua, Gamecore/GamecorePostProcess.lua and two
yield atlases): ModBuddy rebuilds every modinfo from its project at a release, so an installed release has them, but a
test install made from the fork's folders does not, and the master DLL then finds no GamecoreInit.lua (every Lua state
has Game, Map, Players nil). research/06-tooling-and-workflows/new-vp-release-checklist.md step 2.

What a fix does, as 3515's tools/d10/evidence/3515/scripts/s18_fixmodinfo.py (the reference) did and nothing more:
every file that the project imports into the VFS (<Content Include="..."> with <ImportIntoVFS>True) and that the
modinfo does not list gets a line <File md5="<MD5 of the file>" import="1">path</File> before </Files>; a file the
modinfo lists with import="0" that the project imports gets import="1". Nothing else in the modinfo changes (its
Actions, EntryPoints, properties and the other File lines and their md5 stay as committed); the byte-order mark, the
line ends, the indentation and the path separator of the existing lines are kept. A project file that is not in the
folder is reported, never added. A modinfo that already matches its project is not rewritten (Release-5.4.8's three
mods: none changes, tested 2026-11-15 on a copy of the 5.4.8 mod folders).

    python -I tools/release/stages/_modinfo.py check FOLDER [FOLDER ...]   # report only; exit 1 if any would change
    python -I tools/release/stages/_modinfo.py fix FOLDER [FOLDER ...] [--project P.civ5proj]

FOLDER is a mod folder (one .modinfo; its .civ5proj there, or --project names one, e.g. the fork's copy when the
install has none). A fix of an instance's MODS folder also wants the instance's cache/Civ5ModsDatabase.db removed
(backed up first, AGENTS section 4): the game caches the mods' file lists by mod id and version (3515, 6.1d).

Standard-library Python 3.11. Written 2026-11-15 for issue 0310.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

CONTENT_RE = re.compile(r'<Content\s+Include="([^"]+)"\s*(/>|>(.*?)</Content>)', re.S)
VFS_TRUE_RE = re.compile(r'<ImportIntoVFS>\s*True\s*</ImportIntoVFS>', re.I)
FILE_RE = re.compile(r'^([ \t]*)<File\b([^>]*)>([^<]+)</File>[ \t]*(?=\r?$)', re.M)
IMPORT_RE = re.compile(r'\bimport="([01])"')


class ModinfoError(Exception):
    pass


def norm(p: str) -> str:
    return p.strip().replace('\\', '/').lower()


def one(folder: Path, pattern: str) -> Path | None:
    found = sorted(folder.glob(pattern))
    if len(found) > 1:
        raise ModinfoError(f'{folder}: {len(found)} {pattern} files (one expected)')
    return found[0] if found else None


def project_imports(text: str) -> list[str]:
    """The paths a .civ5proj imports into the VFS, in project order (as written there)."""
    out = []
    for inc, _, body in CONTENT_RE.findall(text):
        if body and VFS_TRUE_RE.search(body):
            out.append(inc.strip())
    return out


def real_path(folder: Path, rel: str) -> Path | None:
    """The file `rel` under folder, each part matched without regard to case (the game's VFS ignores case)."""
    p = folder
    for part in [x for x in rel.replace('\\', '/').split('/') if x]:
        if not p.is_dir():
            return None
        # the name as it is on disk (Windows finds `p / part` in any case, but the modinfo line wants the disk's)
        names = [c for c in p.iterdir() if c.name.lower() == part.lower()]
        hit = next((c for c in names if c.name == part), names[0] if names else None)
        if hit is None:
            return None
        p = hit
    return p if p.is_file() else None


def md5(p: Path) -> str:
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for c in iter(lambda: f.read(1 << 20), b''):
            h.update(c)
    return h.hexdigest().upper()


def plan(folder: Path, project: Path | None = None) -> dict:
    """What a fix of the folder's modinfo would change: {'modinfo', 'project', 'add': [[path, md5]], 'flip': [path],
    'missing': [path]}; project None when the folder has no .civ5proj and none is named (nothing to compare)."""
    folder = Path(folder)
    info = one(folder, '*.modinfo')
    if info is None:
        raise ModinfoError(f'{folder}: no .modinfo')
    proj = Path(project) if project else one(folder, '*.civ5proj')
    res = {'modinfo': str(info), 'project': str(proj) if proj else None, 'add': [], 'flip': [], 'missing': []}
    if proj is None:
        return res
    text = info.read_bytes().decode('utf-8-sig')
    files = {}
    for m in FILE_RE.finditer(text):
        imp = IMPORT_RE.search(m.group(2))
        files[norm(m.group(3))] = imp.group(1) if imp else None
    slash = '/' if any('/' in m.group(3) for m in FILE_RE.finditer(text)) or not files else '\\'
    seen = set()
    for inc in project_imports(proj.read_bytes().decode('utf-8-sig', errors='replace')):
        k = norm(inc)
        if k in seen:
            continue
        seen.add(k)
        if k in files:
            if files[k] == '0':
                res['flip'].append(inc.replace('\\', slash).replace('/', slash))
            continue
        real = real_path(folder, inc)
        if real is None:
            res['missing'].append(inc)
            continue
        rel = real.relative_to(folder).as_posix().replace('/', slash)
        res['add'].append([rel, md5(real)])
    return res


def changes(p: dict) -> bool:
    return bool(p['add'] or p['flip'])


def apply(p: dict) -> bool:
    """Write the plan into its modinfo. False (and the file untouched) when there is nothing to change."""
    if not changes(p):
        return False
    info = Path(p['modinfo'])
    raw = info.read_bytes()
    bom = raw.startswith(b'\xef\xbb\xbf')
    text = raw.decode('utf-8-sig')
    nl = '\r\n' if '\r\n' in text else '\n'
    flips = {norm(x) for x in p['flip']}

    def flip(m):
        if norm(m.group(3)) not in flips:
            return m.group(0)
        return m.group(0).replace('import="0"', 'import="1"', 1)
    text = FILE_RE.sub(flip, text)
    if p['add']:
        if text.count('</Files>') != 1:
            raise ModinfoError(f'{info}: {text.count("</Files>")} </Files> tags (one expected)')
        lines = list(FILE_RE.finditer(text))
        indent = lines[-1].group(1) if lines else '    '
        at = text.index('</Files>')
        line_start = text.rfind('\n', 0, at) + 1
        close_indent = text[line_start:at] if not text[line_start:at].strip() else ''
        new = ''.join(f'{indent}<File md5="{h}" import="1">{rel}</File>{nl}' for rel, h in p['add'])
        if close_indent or line_start == at:
            text = text[:line_start] + new + text[line_start:]
        else:  # </Files> shares its line with something else
            text = text[:at] + nl + new + text[at:]
    info.write_bytes((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))
    return True


def fix(folder: Path, project: Path | None = None) -> dict:
    """plan() and apply(); the plan with 'changed' set."""
    p = plan(folder, project)
    p['changed'] = apply(p)
    return p


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['check', 'fix'])
    ap.add_argument('folders', nargs='+', type=Path)
    ap.add_argument('--project', type=Path, help='the .civ5proj to compare with (default: the one in each folder)')
    a = ap.parse_args(argv)
    would = False
    out = []
    try:
        for f in a.folders:
            p = fix(f, a.project) if a.cmd == 'fix' else plan(f, a.project)
            would |= changes(p)
            out.append(p)
    except (ModinfoError, OSError, UnicodeDecodeError) as e:
        print(f'_modinfo: {e}', file=sys.stderr)
        return 2
    print(json.dumps(out, indent=1))
    return 1 if (a.cmd == 'check' and would) else 0


if __name__ == '__main__':
    sys.exit(main())
