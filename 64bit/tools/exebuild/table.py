"""table.py - the EXE symbol catalogue and the per-build address tables, for Python tools and tests (epic 0590, D202).

Ticket 4960. Two inputs:
- the symbol catalogue, committed: exebuild/symbols/<area>.tsv (name, kind, anchor VA in build 4390913, consumer, note);
- the per-build tables, LOCAL ONLY (D202): <exe_tables>/<steam build>/table.json, written by gen_table.py. The folder is
  the setting `exe_tables` of tools/release/settings.py (environment variable C5_EXE_TABLES, else
  deps/local-settings.ini). A table's values are never committed, pushed, or written into research, issues or messages.

    import table                                   # with tools/exebuild on sys.path
    table.symbols()                                # [Symbol], the whole catalogue, validated
    table.anchor('cam_app')                        # 0xCC8E74, the VA in build 4390913
    table.va('26577625', 'cam_app')                # the carried VA from the local table, None when not carried
    table.load('26577625')                         # the whole table.json as a dict
    table.tables_dir()                             # Path of the exe_tables setting (TableError names the setting)

Formats: research/02-exe-internals/exe-build-table.md sections 3 and 5. Standard library only, Python 3.8+.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import subprocess
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Tuple

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SYMBOLS_DIR = REPO / 'exebuild' / 'symbols'
PATTERNS_LIST = REPO / 'exebuild' / 'patterns.txt'
AREAS = ('proxy', 'shell', 'render', 'server')
KINDS = ('code', 'data', 'rdata', 'shader', 'iat', 'vtable', 'array')
ARRAY_COL_KINDS = ('code', 'data', 'rdata', 'shader', 'keep')
# <area>_<rest>: lower case, at least one underscore (a pattern operand R(x) with an underscore is a name, without one a
# hex VA), no trailing underscore.
NAME_RE = re.compile(r'^[a-z][a-z0-9]*_[a-z0-9_]*[a-z0-9]$')
HEADER = ['name', 'kind', 'anchor_va', 'consumer', 'note']
SETTING = 'exe_tables'
SETTING_ENV = 'C5_EXE_TABLES'
GEN_CMD = ('python -I tools/exebuild/gen_table.py --old <DX11 EXE of build 4390913> --new <DX11 EXE of build {b}> '
           '--steam-build {b}')
IDENTITY_CMD = 'python -I tools/exebuild/gen_table.py --identity --old <DX11 EXE of build 4390913>'
# The workstation's read-only golden copies of each known build's DX11 EXE (ticket 0583; epic 0590), so that an error
# can give the exact commands. Another machine passes its own copies to gen_table.py.
GOLDEN_EXE = {
    '4390913': 'F:/c5-64bit-work/golden/civ5-steam-build4390913-ws/CivilizationV_DX11.exe',
    '26577625': 'F:/c5-64bit-work/golden/civ5-steam-build26577625-ws/CivilizationV_DX11.exe',
}
# Names that must never be in git (D202): the generated tables, the hand answers, the review list, the generated header.
FORBIDDEN_NAMES = ('table.json', 'hand-review.tsv', 'review.tsv', 'vp_exe_tables.gen.h')


def gen_command(steam_build: str) -> str:
    """The exact gen_table.py command that makes a build's table for this checkout's catalogue (the golden copies of
    GOLDEN_EXE; a build without one gets the placeholder form)."""
    old = GOLDEN_EXE.get('4390913')
    if str(steam_build) == '4390913':
        return f'python -I tools/exebuild/gen_table.py --identity --old {old}' if old else IDENTITY_CMD
    new = GOLDEN_EXE.get(str(steam_build))
    if old and new:
        return f'python -I tools/exebuild/gen_table.py --old {old} --new {new} --steam-build {steam_build}'
    return GEN_CMD.format(b=steam_build)


def gen_commands() -> List[str]:
    """gen_command for every known build, anchor build first: what makes a checkout's tables in one step."""
    return [gen_command(b.steam_build) for b in builds.KNOWN]


class CatalogueError(Exception):
    pass


class TableError(Exception):
    pass


class Symbol(NamedTuple):
    name: str
    kind: str
    anchor: Optional[int]          # VA in build 4390913; None for an array
    consumer: str
    note: str
    area: str
    where: str                     # file:line of the row
    array_file: Optional[str] = None    # array: repository path of the anchor list
    array_cols: Tuple[str, ...] = ()    # array: one kind per column (ARRAY_COL_KINDS)


def _builds():
    spec = importlib.util.spec_from_file_location('c5_exebuild_builds', HERE / 'builds.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


builds = _builds()


def _settings():
    spec = importlib.util.spec_from_file_location('c5_release_settings', REPO / 'tools' / 'release' / 'settings.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def iat_parts(note: str) -> Tuple[str, str]:
    """`DLL!Function` -> (dll, function); CatalogueError when the note is not in that form."""
    dll, sep, fn = (note or '').strip().partition('!')
    if not sep or not dll or not fn or ' ' in dll:
        raise CatalogueError(f'iat note {note!r} is not DLL!Function')
    return dll, fn.split()[0]


def parse_array_note(note: str) -> Tuple[str, Tuple[str, ...]]:
    """`file=<repository path>;cols=<kind>,<kind>,...` -> (path, cols)."""
    fields = {}
    for part in (note or '').split(';'):
        k, sep, v = part.strip().partition('=')
        if sep:
            fields[k.strip()] = v.strip()
    f, cols = fields.get('file'), tuple(c.strip() for c in fields.get('cols', '').split(',') if c.strip())
    if not f or not cols:
        raise CatalogueError(f'array note {note!r} needs file=<path>;cols=<kind,...>')
    bad = [c for c in cols if c not in ARRAY_COL_KINDS]
    if bad:
        raise CatalogueError(f'array column kinds {bad} not in {ARRAY_COL_KINDS}')
    if all(c == 'keep' for c in cols):
        raise CatalogueError('array has no address column')
    return f, cols


def symbols(symbols_dir: Optional[Path] = None) -> List[Symbol]:
    """The whole catalogue in area order (AREAS, then any other <area>.tsv by name), each file in row order.
    Validates names (NAME_RE, unique across files), kinds, anchors (hex) and the iat/array notes."""
    d = Path(symbols_dir) if symbols_dir else SYMBOLS_DIR
    files = sorted(d.glob('*.tsv'), key=lambda p: (AREAS.index(p.stem) if p.stem in AREAS else len(AREAS), p.stem))
    out: List[Symbol] = []
    seen: Dict[str, str] = {}
    anchors: Dict[int, Tuple[str, str]] = {}
    for f in files:
        header_seen = False
        for n, line in enumerate(f.read_text(encoding='utf-8').splitlines(), 1):
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            cells = line.split('\t')
            where = f'{f.name}:{n}'
            if not header_seen:
                if [c.strip() for c in cells] != HEADER:
                    raise CatalogueError(f'{where}: the first row must be the header {"<TAB>".join(HEADER)}')
                header_seen = True
                continue
            if len(cells) != len(HEADER):
                raise CatalogueError(f'{where}: {len(cells)} columns, want {len(HEADER)} (tab-separated)')
            name, kind, av, consumer, note = (c.strip() for c in cells)
            if not NAME_RE.match(name):
                raise CatalogueError(f'{where}: name {name!r} is not <area>_<rest> in [a-z0-9_]')
            if name in seen:
                raise CatalogueError(f'{where}: name {name} already at {seen[name]}')
            seen[name] = where
            if kind not in KINDS:
                raise CatalogueError(f'{where}: kind {kind!r} not in {KINDS}')
            afile, acols, anchor = None, (), None
            if kind == 'array':
                if av not in ('', '-'):
                    raise CatalogueError(f'{where}: an array row has anchor_va `-` (the anchors are in its file)')
                afile, acols = parse_array_note(note)
                if not (REPO / afile).is_file():
                    raise CatalogueError(f'{where}: array file {afile} not found')
            else:
                if not re.match(r'^0x[0-9A-Fa-f]{1,8}$', av):
                    raise CatalogueError(f'{where}: anchor_va {av!r} is not a hex VA (0x...)')
                anchor = int(av, 16)
                if kind == 'iat':
                    iat_parts(note)
                # one row per anchor across all files (a consumer reuses the existing name)
                if anchor in anchors:
                    raise CatalogueError(f'{where}: {av} is already catalogued as {anchors[anchor][1]}')
                anchors[anchor] = (kind, name)
            if not consumer:
                raise CatalogueError(f'{where}: no consumer')
            out.append(Symbol(name, kind, anchor, consumer, note, f.stem, where, afile, acols))
        if not header_seen:
            raise CatalogueError(f'{f.name}: no header row')
    return out


_HASH_CACHE: Dict[Tuple[str, Tuple[Tuple[str, int, int], ...]], str] = {}


def catalogue_hash(symbols_dir: Optional[Path] = None) -> str:
    """_catalogue_hash, remembered per folder while no catalogue or array file changes (size and mtime)."""
    d = Path(symbols_dir) if symbols_dir else SYMBOLS_DIR
    files = list(d.glob('*.tsv')) + [REPO / s.array_file for s in symbols(symbols_dir) if s.kind == 'array']
    key = (str(d), tuple(sorted((str(f), f.stat().st_size, f.stat().st_mtime_ns) for f in files)))
    if key not in _HASH_CACHE:
        _HASH_CACHE[key] = _catalogue_hash(symbols_dir)
    return _HASH_CACHE[key]


def _catalogue_hash(symbols_dir: Optional[Path] = None) -> str:
    """SHA-256 over the catalogue's parsed rows (not the comments) and the rows of every array file (ticket 4966): a
    table generated from another catalogue or another anchor list is stale."""
    h = hashlib.sha256()
    for s in symbols(symbols_dir):
        h.update(('\t'.join([s.name, s.kind, '%X' % s.anchor if s.anchor is not None else '-', s.note]) + '\n')
                 .encode('utf-8'))
        if s.kind == 'array':
            for row in parse_array_file(REPO / s.array_file, len(s.array_cols)):
                h.update((','.join('%X' % v for v in row) + '\n').encode('utf-8'))
    return h.hexdigest()


def by_name(symbols_dir: Optional[Path] = None) -> Dict[str, Symbol]:
    return {s.name: s for s in symbols(symbols_dir)}


def anchor(name: str, symbols_dir: Optional[Path] = None) -> int:
    s = by_name(symbols_dir).get(name)
    if s is None:
        raise KeyError(f'no symbol {name} in the catalogue (exebuild/symbols/*.tsv)')
    if s.anchor is None:
        raise KeyError(f'{name} is an array; its anchors are in {s.array_file}')
    return s.anchor


NUM_RE = re.compile(r'-?(?:0[xX][0-9A-Fa-f]+|\d+)[uU]?')


def parse_array_file(path, ncols: int) -> List[List[int]]:
    """Rows `{v, v, ...},` of a generated C table (one row per line; `//` comments skipped). Every row must have ncols
    numbers. Returns the rows as ints."""
    rows = []
    for n, line in enumerate(Path(path).read_text(encoding='utf-8').splitlines(), 1):
        s = line.split('//')[0].strip()
        if not s.startswith('{'):
            continue
        body = s[1:s.index('}')] if '}' in s else s[1:]
        vals = [int(v.rstrip('uU'), 0) for v in NUM_RE.findall(body)]
        if len(vals) != ncols:
            raise CatalogueError(f'{path}:{n}: {len(vals)} values, the catalogue row says {ncols} columns')
        rows.append(vals)
    if not rows:
        raise CatalogueError(f'{path}: no rows')
    return rows


def tables_dir(environ=None) -> Path:
    """The exe_tables setting (tools/release/settings.py): environment variable C5_EXE_TABLES, else the `exe_tables`
    line of deps/local-settings.ini. TableError names both and the generator command when it is not set."""
    st = _settings()
    try:
        v = st.get(SETTING, environ)
    except st.SettingError as e:
        raise TableError(f'settings: {e}')
    if v is None:
        raise TableError(
            f'the setting `{SETTING}` is not set: set the environment variable {SETTING_ENV} or `{SETTING} = <folder>` '
            f'in deps/local-settings.ini (tools/release/settings.py), then generate the tables with '
            f'`{"` and `".join(gen_commands())}` (research/02-exe-internals/exe-build-table.md section 6)')
    return Path(v)


def _steam_build(build) -> str:
    if hasattr(build, 'steam_build'):
        return build.steam_build
    if isinstance(build, int):
        for b in builds.KNOWN:
            if b.id == build:
                return b.steam_build
        raise TableError(f'unknown build id {build}')
    return str(build)


def table_path(build, tables: Optional[Path] = None, environ=None) -> Path:
    return Path(tables or tables_dir(environ)) / _steam_build(build) / 'table.json'


def catalogue_table_path(build, chash: str, tables: Optional[Path] = None, environ=None) -> Path:
    """The copy of a build's table made from the catalogue with hash chash: <build>/table-<first 16 hex>.json. gen_table
    writes it beside table.json, so worktrees whose catalogues differ (two carriers adding rows at once) each find the
    table of their own catalogue in the one shared exe_tables folder (ticket 4966)."""
    return Path(tables or tables_dir(environ)) / _steam_build(build) / f'table-{chash[:16]}.json'


def load(build, tables: Optional[Path] = None, environ=None, chash: Optional[str] = None) -> dict:
    """The table of a build (steam build string, builds.Build or id): table-<chash>.json when chash is given and it
    exists, else table.json (the last one generated). TableError naming the setting and the generator command when it
    is missing or unreadable."""
    sb = _steam_build(build)
    p = table_path(sb, tables, environ)
    if chash:
        pc = catalogue_table_path(sb, chash, tables, environ)
        if pc.is_file():
            p = pc
    if not p.is_file():
        raise TableError(f'no address table for build {sb}: {p} is missing (setting `{SETTING}` / {SETTING_ENV}); '
                         f'generate it with `{gen_command(sb)}`')
    try:
        t = json.loads(p.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        raise TableError(f'{p}: not readable as JSON ({e})')
    if t.get('format') != 1 or str(t.get('steam_build')) != sb:
        raise TableError(f'{p}: not a format-1 table of build {sb}')
    return t


def va(build, name: str, tables: Optional[Path] = None, environ=None) -> Optional[int]:
    """The VA of a symbol in a build (ImageBase 0x400000), None when the table does not carry it. Reads the table of
    this checkout's catalogue when there is one."""
    e = load(build, tables, environ, catalogue_hash()).get('symbols', {}).get(name)
    return int(e['va'], 16) if e and e.get('va') else None


def array(build, name: str, tables: Optional[Path] = None, environ=None) -> Optional[List[List[int]]]:
    """An array symbol's rows in a build (a cell 0 = not carried), None when the table has no such array."""
    e = load(build, tables, environ, catalogue_hash()).get('arrays', {}).get(name)
    return [[int(c, 16) if isinstance(c, str) else c for c in row] for row in e['rows']] if e else None


def tracked_forbidden(repo: Optional[Path] = None) -> List[str]:
    """Paths git tracks (or has staged) whose name must never be committed (FORBIDDEN_NAMES, D202). [] = clean."""
    r = Path(repo or REPO)
    out = []
    for args in (['ls-files'], ['diff', '--cached', '--name-only']):
        cp = subprocess.run(['git', '-C', str(r)] + args, capture_output=True, text=True, errors='replace')
        if cp.returncode != 0:
            raise TableError(f'git {" ".join(args)} failed: {cp.stderr.strip()}')
        for p in cp.stdout.splitlines():
            n = p.rsplit('/', 1)[-1]
            if n in FORBIDDEN_NAMES or (n.startswith('table-') and n.endswith('.json')):
                out.append(p)
    return sorted(set(out))
