"""The test-data stage of the release driver (epic 3500, issue 3501; new-vp-release-checklist.md step 2).

For a release tag it makes, under <out> (the driver's <work>/testdata):

  mods/<folder>/                 the tag's mod folders (git archive of the fork at the tag): the descriptor's mod set
                                 (CP, VP) and, when the tag ships it, the EUI compatibility folder "(3a) ...", which
                                 goes into the database and the modpack but not into the descriptor's mods (MC ruling
                                 2026-11-13: the test database is what a player installs from the tag); each folder's
                                 .modinfo given the VFS imports its .civ5proj has and the committed modinfo lacks, as
                                 ModBuddy's release build would (issue 0310, stages/_modinfo.py: upstream master's CP
                                 modinfo lacks Gamecore/GamecoreInit.lua and three more; a 5.4.8 tag changes nothing)
  game/<game folder name>/       the game folder for x86 runs: a copy of the base descriptor's game folder with its VP
                                 modpack DLC folder (Assets/DLC/VP_MODPACK_*) replaced by Assets/DLC/VP_MODPACK_<tag>
                                 holding the base modpack's .Civ5Pkg and Mods/<the tag's mod folders>
  db/snapshot.db                 the DLC snapshot of that game folder (c5h snapshot --out, from a copy of the core database)
  db/Civ5DebugDatabase.db        c5h dbbuild of the tag's mods on that snapshot, post-processed with the tag's DLL, with
                                 CustomModOptions SQLITE_LOGGING = 1 (as the pinned cdrift database has it)
  compare-<base tag>.json        with --compare (default when the tag is the base descriptor's): the built database against
                                 the base descriptor's test_db, table by table (row multisets)
  <tag>.json                     the candidate descriptor (validated by descriptor.parse): reference saves from the
                                 base descriptor (premise P10: the driver tries the old save first), reference counts {}
                                 until the save test fills them; for the base release itself test_db stays the base's
                                 (pinned) database unless the build reproduced it exactly
  testdata.json                  what was done, the inputs' hashes, the timings

  python -I tools/release/stages/testdata.py --tag Release-5.4.8 --out C:/c5-64bit-work/3500/releases/Release-5.4.8/testdata
         [--fork CLONE] [--base Release-5.4.8] [--core CORE.db]
         [--dll DLL] [--no-compare]

--fork (and ctx.fork) default: the fork clone setting of tools/x64-dll/dll_source.py (VP_DLL_FORK, else `fork =` in
deps/dll-source.ini, else the DLL source location; ticket 4709).

Nothing is written outside --out: civ5-headless is used as a tool through its own c5h.cmd with C5H_CONFIG pointing at a
configuration file written under <out>/c5h (game folder, an empty user folder, database cache and runs under <out>), and
the core database (a base-game Civ5CoreDatabase.db, release-independent, pinned by SHA-256 in CORE_DB_PINS: never the
game's live cache, ticket 0612) is copied into <out> before use. A file <out>/STOP ends the stage before its next step (exit 10). Exit 0 = done; 1 = a step
failed; 2 = bad arguments or descriptor; 10 = stopped.

run(ctx) is the driver's entry point (tools/release/README.md section 2): ctx.tag, ctx.work (Path), optionally
ctx.descriptor (the base Descriptor), ctx.fork (Path) and ctx.run(cmd, **kw) (the driver's gated runner; subprocess.run
otherwise). It returns {'status': 'ok'|'stop'|'failed', 'outputs': {...}, 'stop': {...}?, 'error': str?}. Every stage
after this one reads the candidate descriptor (outputs 'descriptor'), and the tools they start get C5_RELEASE naming it
(issue 3512). For a new release the base is the previous release's descriptor (take_release.py --base-release): the
candidate then has the built database. The candidate also carries player_dll_43_sha256 (the tag's "(3b) 43 Civs
Community Patch" DLL, else the base's for the base release itself).

Standard-library Python 3.11. Written 2026-11-13 by team-3500 W1a.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tarfile
import time
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import descriptor  # noqa: E402
import settings as release_settings  # noqa: E402
from stages import _modinfo  # noqa: E402

# Ticket 4711: the settings c5h and core_db of tools/release/settings.py (C5_C5H, C5_CORE_DB; c5h defaults to
# <home>/Documents/civ5-headless; core_db has no default since ticket 0612: the pinned copy, see CORE_DB_PINS).
C5H = release_settings.get('c5h')


def _load_dll_source():
    """tools/x64-dll/dll_source.py, loaded by path (its folder name is not a package name)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('dll_source', HERE.parents[2] / 'tools' / 'x64-dll' / 'dll_source.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# The fork clone holding the release tags (ticket 4709): VP_DLL_FORK, else `fork =` of deps/dll-source.ini, else the DLL
# source setting (tools/x64-dll/dll_source.py). (Replaced: a fixed path of the workstation's clone.)
FORK = _load_dll_source().fork_clone()
CORE_DB = release_settings.get('core_db')
CP_GUID = 'd1b6328c-ff44-4b0d-aad7-c657f83610cd'
TAG_DB_PREFIX = '(3a)'   # the tag's EUI compatibility folder ("(3a) VP - EUI Compatibility Files" in 5.4.8)
PLAYER_DLL_43 = '(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll'   # stages/pack.py's (issue 4252)
STOPPED = 10


class StageError(Exception):
    pass


class Stopped(Exception):
    pass


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for c in iter(lambda: f.read(1 << 20), b''):
            h.update(c)
    return h.hexdigest()


def now() -> str:
    return time.strftime('%Y-%m-%d %H:%M:%S')


# Ticket 0612: the core database (c5h snapshot's first layer, base game XML only) is pinned by SHA-256, never the game's
# live cache, which a start can rewrite as the fully loaded database (DLC and mod rows in it: 'not unique' errors and
# doubled rows). The same file serves EXE build 26577625 and the build before it (research release-driver.md W1a.3).
# Re-pin after a game update: new-vp-release-checklist.md step 2.
CORE_DB_PINS = {'dec462184517315c72753029bc3f84f86244528c31ba505eb7cba5cef45706f2':
                'base-game core database, pre-26577625 cache of 2026-11-18; serves builds before and at 26577625'}


def check_core(core, pins=None) -> str:
    """The core database's SHA-256 when it is pinned; else StageError with one line (D118: no quiet fallback)."""
    pins = CORE_DB_PINS if pins is None else pins
    if core is None:
        raise StageError('no core database: set C5_CORE_DB or `core_db =` in the settings file to the pinned copy')
    if not Path(core).is_file():
        raise StageError(f'core database {core} is missing (pinned SHA-256: {", ".join(pins)})')
    h = sha256(Path(core))
    if h not in pins:
        raise StageError(f'core database {core} has SHA-256 {h}, not a pinned one ({", ".join(pins)}); '
                         'a live game cache is never used (ticket 0612)')
    return h


def copy_game(src: Path, dst: Path, skip_packs) -> None:
    """Copy the base game folder, leaving out the VP modpack folders `skip_packs` under Assets/DLC (no symlinks)."""
    src, dst = Path(src), Path(dst)
    dlc = src / 'Assets' / 'DLC'

    def ignore(d, names):
        return [n for n in names if Path(d) == dlc and n in skip_packs]
    shutil.copytree(src, dst, ignore=ignore, symlinks=False)


def modpack_name(tag: str) -> str:
    """The DLC folder name of the stage's modpack: VP_MODPACK_<tag without Release-, dots to underscores>."""
    return 'VP_MODPACK_' + re.sub(r'[^A-Za-z0-9]+', '_', re.sub(r'^Release-', '', tag)).strip('_').upper()


def read_modinfo(folder: Path) -> tuple[str, int]:
    """(guid lower case without braces, version) from the one .modinfo in folder."""
    infos = sorted(folder.glob('*.modinfo'))
    if len(infos) != 1:
        raise StageError(f'{folder}: {len(infos)} .modinfo files (one expected)')
    root = ET.parse(infos[0]).getroot()
    guid, ver = root.get('id'), root.get('version')
    if not guid or not ver or not ver.isdigit():
        raise StageError(f'{infos[0]}: no Mod id/version attributes')
    return guid.strip('{}').lower(), int(ver)


def table_rows(con: sqlite3.Connection, schema: str, table: str) -> tuple[list[str], dict]:
    """(sorted column names, {row tuple in that column order: count})."""
    cols = sorted(r[1] for r in con.execute(f'PRAGMA {schema}.table_info("{table}")'))
    if not cols:
        return [], {}
    out: dict = {}
    sel = ', '.join(f'"{c}"' for c in cols)
    for r in con.execute(f'SELECT {sel} FROM {schema}."{table}"'):
        out[r] = out.get(r, 0) + 1
    return cols, out


def compare_dbs(a: Path, b: Path, examples: int = 3) -> dict:
    """Table-by-table comparison of two game databases (row multisets, columns by name; sqlite_* tables skipped)."""
    con = sqlite3.connect(f'file:{a.as_posix()}?mode=ro', uri=True)
    con.execute(f"ATTACH DATABASE 'file:{b.as_posix()}?mode=ro' AS b")
    names = {s: {r[0] for r in con.execute(f"SELECT name FROM {s}.sqlite_master WHERE type = 'table' "
                                            f"AND name NOT LIKE 'sqlite_%'")} for s in ('main', 'b')}
    res = {'a': str(a), 'b': str(b), 'only_in_a': sorted(names['main'] - names['b']),
           'only_in_b': sorted(names['b'] - names['main']), 'tables_compared': 0, 'identical_tables': 0, 'differ': []}
    for t in sorted(names['main'] & names['b']):
        ca, ra = table_rows(con, 'main', t)
        cb, rb = table_rows(con, 'b', t)
        res['tables_compared'] += 1
        if ca != cb:
            res['differ'].append({'table': t, 'columns_only_in_a': sorted(set(ca) - set(cb)),
                                  'columns_only_in_b': sorted(set(cb) - set(ca)),
                                  'rows_a': sum(ra.values()), 'rows_b': sum(rb.values())})
            continue
        if ra == rb:
            res['identical_tables'] += 1
            continue
        only_a = [r for r in ra if ra[r] > rb.get(r, 0)]
        only_b = [r for r in rb if rb[r] > ra.get(r, 0)]
        res['differ'].append({'table': t, 'rows_a': sum(ra.values()), 'rows_b': sum(rb.values()),
                              'rows_only_in_a': sum(ra[r] - rb.get(r, 0) for r in only_a),
                              'rows_only_in_b': sum(rb[r] - ra.get(r, 0) for r in only_b),
                              'columns': ca,
                              'examples_a': [list(map(str, r)) for r in sorted(only_a, key=str)[:examples]],
                              'examples_b': [list(map(str, r)) for r in sorted(only_b, key=str)[:examples]]})
    con.close()
    res['identical'] = not (res['only_in_a'] or res['only_in_b'] or res['differ'])
    return res


class Stage:
    def __init__(self, tag: str, out: Path, fork: Path, base: descriptor.Descriptor, *, core: Path = CORE_DB,
                 dll: Path | None = None, compare: bool | None = None, c5h: Path = C5H, runner=None, log=print,
                 extra_mods: list | None = None, heavy=None, ref: str | None = None, core_pins: dict | None = None):
        # ref: the git revision of the release in the fork when it is not the tag (take_release.py --base-commit,
        # issue 3509); None = the tag
        # heavy(name, cmd, cwd=, env=) -> CompletedProcess-like: the heavy steps (the game-folder copy, c5h snapshot
        # and dbbuild) go through it so that they start under quietlock.py run (3502, lead 2026-11-13: the copy took
        # 371 s on a loaded machine). None: c5h through `runner` and the copy in this process (the unit tests).
        self.heavy = heavy
        self.tag, self.out, self.fork, self.base, self.dll = tag, Path(out), Path(fork), base, dll
        self.core, self.core_pins = (Path(core) if core else None), core_pins   # ticket 0612: checked in go()
        self.ref = ref or tag
        self.compare = (tag == base.tag) if compare is None else compare
        self.extra_mods = [Path(m) for m in (extra_mods or [])]
        self.c5h, self.log = Path(c5h), log
        self.runner = runner or (lambda cmd, **kw: subprocess.run(cmd, **kw))
        self.record: dict = {'tag': tag, 'base': base.tag, 'started': now(), 'steps': {}}

    # -- helpers
    def check_stop(self, step: str):
        if (self.out / 'STOP').exists():
            raise Stopped(f'STOP file before step {step}')

    def run(self, cmd, **kw):
        kw.setdefault('capture_output', True)
        kw.setdefault('text', True)
        kw.setdefault('errors', 'replace')
        return self.runner([str(c) for c in cmd], **kw)

    def heavy_run(self, name, cmd, cwd=None, env=None):
        if self.heavy:
            return self.heavy(name, [str(c) for c in cmd], cwd=cwd, env=env)
        return self.run(cmd, env=env, cwd=cwd)

    def git(self, *args, text=True):
        p = self.run(['git', '-C', self.fork, *args], text=text, errors=None if not text else 'replace')
        if p.returncode != 0:
            err = p.stderr if text else p.stderr.decode('utf-8', 'replace')
            raise StageError(f'git {" ".join(args)}: {err.strip()[-500:]}')
        return p.stdout

    def step(self, name, fn):
        self.check_stop(name)
        t0 = time.monotonic()
        self.log(f'[{time.strftime("%H:%M:%S")}] testdata: {name}')
        r = fn()
        self.record['steps'][name] = dict(r or {}, seconds=round(time.monotonic() - t0, 2))
        self.save_record()
        return r

    def save_record(self):
        (self.out / 'testdata.json').write_text(json.dumps(self.record, indent=1) + '\n', encoding='utf-8')

    def tag_file_sha256(self, rel: str) -> str | None:
        """SHA-256 of a file of the fork at the release's commit, or None when the commit has no such file."""
        p = self.run(['git', '-C', self.fork, 'show', f'{self.commit}:{rel}'], text=False, errors=None)
        return hashlib.sha256(p.stdout).hexdigest() if p.returncode == 0 and p.stdout else None

    # -- steps
    def s_commit(self):
        self.commit = self.git('rev-parse', f'{self.ref}^{{commit}}').strip()
        return {'commit': self.commit}

    def s_mods(self):
        dst = self.out / 'mods'
        if dst.exists():
            shutil.rmtree(dst)
        dst.mkdir(parents=True)
        folders = self.base.mod_folders
        # MC ruling 2026-11-13 (epic thread #203): the test database is what a player installs from the tag: the
        # descriptor's mods (CP, VP) plus the EUI compatibility files when the tag ships them (database and modpack
        # only: saves do not name it, so it is not in the descriptor's mod set).
        top = self.git('ls-tree', '--name-only', self.commit).splitlines()
        self.tag_db_folders = [n for n in top if n.startswith(TAG_DB_PREFIX) and 'EUI' in n and n not in folders]
        tar = self.git('archive', '--format=tar', self.commit, *folders, *self.tag_db_folders, text=False)
        with tarfile.open(fileobj=io.BytesIO(tar)) as t:
            t.extractall(dst, filter='data')
        # issue 0310: the modinfo a release install has (ModBuddy rebuilds it from the .civ5proj), before anything reads
        # it: the database build, the modpack copy and the instance installs from these folders
        regen = {}
        for f in [*folders, *self.tag_db_folders]:
            if (dst / f).is_dir():
                p = _modinfo.fix(dst / f)
                regen[f] = {'changed': p['changed'], 'added': [r for r, _ in p['add']], 'import_set': p['flip'],
                            'project_files_missing': p['missing'], 'project': Path(p['project']).name
                            if p['project'] else None}
        self.modinfo_regen = regen
        self.mods = []
        for f in folders:
            if not (dst / f).is_dir():
                raise StageError(f'the tag {self.tag} has no mod folder {f!r}')
            guid, ver = read_modinfo(dst / f)
            self.mods.append({'folder': f, 'guid': guid, 'version': ver})
        self.tag_db_mods = []
        for f in self.tag_db_folders:
            guid, ver = read_modinfo(dst / f)
            self.tag_db_mods.append({'folder': f, 'guid': guid, 'version': ver})
        cp = next((m for m in self.mods if m['guid'] == CP_GUID), None)
        self.player_dll = dst / cp['folder'] / 'CvGameCore_Expansion2.dll' if cp else None
        if self.player_dll is not None and not self.player_dll.is_file():
            self.player_dll = None
        return {'mods': self.mods, 'tag_db_mods': self.tag_db_mods, 'modinfo_regenerated': self.modinfo_regen,
                'player_dll_sha256': sha256(self.player_dll) if self.player_dll else None}

    def s_game(self):
        src = self.base.game_dir
        dst = self.out / 'game' / src.name
        dlc = src / 'Assets' / 'DLC'
        packs = sorted(d.name for d in dlc.iterdir() if d.is_dir() and d.name.upper().startswith('VP_MODPACK'))
        if len(packs) > 1:
            raise StageError(f'{dlc}: more than one VP modpack folder: {packs}')
        if dst.parent.exists():
            shutil.rmtree(dst.parent)
        dst.parent.mkdir(parents=True)

        if self.heavy:
            p = self.heavy_run('game-copy', [sys.executable, '-I', Path(__file__).resolve(), '--copy-game', src, dst,
                                             *packs])
            if p.returncode != 0 or not dst.is_dir():
                raise StageError(f'game folder copy exit {p.returncode}: {(p.stdout or "")[-400:]}')
        else:
            copy_game(src, dst, packs)
        new = dst / 'Assets' / 'DLC' / modpack_name(self.tag)
        (new / 'Mods').mkdir(parents=True)
        pkgs = sorted((dlc / packs[0]).glob('*.Civ5Pkg')) if packs else []
        for p in pkgs:
            shutil.copy2(p, new / p.name)
        for m in self.mods + self.tag_db_mods:
            shutil.copytree(self.out / 'mods' / m['folder'], new / 'Mods' / m['folder'], symlinks=False)
        self.game_dir, self.modpack = dst, new
        n = sum(1 for _ in dst.rglob('*'))
        return {'from': str(src), 'game_dir': str(dst), 'left_out': packs, 'modpack': new.name,
                'package_files': [p.name for p in pkgs], 'entries': n}

    def s_config(self):
        cfgdir = self.out / 'c5h'
        if cfgdir.exists():
            shutil.rmtree(cfgdir)
        (cfgdir / 'user').mkdir(parents=True)
        core = cfgdir / 'core' / 'Civ5CoreDatabase.db'
        core.parent.mkdir()
        shutil.copyfile(self.core, core)
        self.core_copy = core
        if sha256(core) != self.core_sha256:
            raise StageError(f'the copy of the core database {core} differs from the pinned {self.core_sha256}')

        def q(p):
            return json.dumps(Path(p).as_posix())  # a TOML basic string is a JSON string for these paths
        toml = cfgdir / 'civ5headless.toml'
        toml.write_text('\n'.join([
            '# Written by tools/release/stages/testdata.py (issue 3501): c5h as a tool, everything under this folder.',
            f'game_dir = {q(self.game_dir)}', f'user_dir = {q(cfgdir / "user")}',
            f'db_cache = {q(cfgdir / "dbcache")}', f'runs = {q(cfgdir / "runs")}', '']), encoding='utf-8')
        self.env = dict(os.environ, C5H_CONFIG=str(toml))
        return {'config': str(toml), 'core': str(self.core), 'core_sha256': self.core_sha256}

    def c5h_cmd(self, *args):
        return ['cmd', '/c', str(self.c5h / 'c5h.cmd'), *args]

    def s_snapshot(self):
        snap = self.out / 'db' / 'snapshot.db'
        snap.parent.mkdir(parents=True, exist_ok=True)
        p = self.heavy_run('snapshot', self.c5h_cmd('snapshot', '--out', snap, '--core', self.core_copy), env=self.env,
                           cwd=str(self.out))
        (self.out / 'db' / 'snapshot.console.txt').write_text(p.stdout + p.stderr, encoding='utf-8')
        benign = []
        if p.returncode != 0 and snap.is_file():
            # c5h snapshot exits 1 on any failed step. The world-builder scripts of DLC_SP_Maps_2/_3 (WB_Script_*.lua)
            # have no GetMapScriptInfo; the game does not register them either (no WB_Script row in the pinned
            # 5.4.8 database, 2026-11-13). Those failures alone are accepted and listed.
            rep = snap.with_name(snap.name + '.build') / 'builddb-report.json'
            steps = json.loads(rep.read_text(encoding='utf-8')).get('steps', []) if rep.is_file() else []
            bad = [s for s in steps if not s.get('ok')]
            benign = [Path(s.get('path', '').replace('\\', '/')).name for s in bad
                      if s.get('kind') == 'mapscript' and (s.get('note') or '').startswith('no GetMapScriptInfo')
                      and Path(s.get('path', '').replace('\\', '/')).name.startswith('WB_Script_')]
            if not bad or len(benign) != len(bad):
                benign = []
        if (p.returncode != 0 and not benign) or not snap.is_file():
            raise StageError(f'c5h snapshot exit {p.returncode}: {(p.stdout + p.stderr).strip()[-800:]}')
        self.snapshot = snap
        return {'snapshot': str(snap), 'sha256': sha256(snap), 'exit': p.returncode,
                'accepted_failures': sorted(benign)}

    def s_dbbuild(self):
        db = self.out / 'db' / 'Civ5DebugDatabase.db'
        dll = self.dll or self.player_dll
        if dll is None:
            raise StageError('no DLL for post-processing: the tag has no (1) Community Patch DLL; pass --dll')
        path_as = 'Assets\\DLC\\' + self.modpack.name + '\\Mods'
        cmd = self.c5h_cmd('dbbuild', '--mods-dir', self.out / 'mods', '--out', db, '--snapshot', self.snapshot,
                           '--game-dir', self.game_dir, '--dll', dll, '--path-as', path_as)
        for m in self.mods + self.tag_db_mods:
            cmd += ['--mod', f"{m['guid']}:{m['version']}"]
        extras = []
        for src in self.extra_mods:  # in the database only, not in the descriptor's mod set (saves do not name them)
            if not src.is_dir():
                raise StageError(f'--extra-mod {src}: no such folder')
            if (self.out / 'mods' / src.name).exists():
                raise StageError(f'--extra-mod {src}: a mod folder {src.name!r} is already there')
            shutil.copytree(src, self.out / 'mods' / src.name, symlinks=False)
            guid, ver = read_modinfo(self.out / 'mods' / src.name)
            extras.append({'folder': src.name, 'guid': guid, 'version': ver, 'from': str(src)})
            cmd += ['--mod', f'{guid}:{ver}']
        self.db_extras = extras
        p = self.heavy_run('dbbuild', cmd, env=self.env, cwd=str(self.out))
        (self.out / 'db' / 'dbbuild.console.txt').write_text(p.stdout + p.stderr, encoding='utf-8')
        if p.returncode != 0 or not db.is_file():
            raise StageError(f'c5h dbbuild exit {p.returncode}: {(p.stdout + p.stderr).strip()[-800:]}')
        con = sqlite3.connect(db)
        before = con.execute("SELECT Value FROM CustomModOptions WHERE Name = 'SQLITE_LOGGING'").fetchall()
        if not before:
            con.close()
            raise StageError(f'{db}: no CustomModOptions row SQLITE_LOGGING')
        con.execute("UPDATE CustomModOptions SET Value = 1 WHERE Name = 'SQLITE_LOGGING'")
        con.commit()
        self.strategies = con.execute('SELECT COUNT(*) FROM AIEconomicStrategies').fetchone()[0]
        con.close()
        self.db = db
        return {'db': str(db), 'sha256': sha256(db), 'dll': str(dll), 'dll_sha256': sha256(dll), 'path_as': path_as,
                'sqlite_logging_before': before[0][0], 'economic_strategies': self.strategies,
                'build_dir': str(db.with_name(db.name + '.build')), 'extra_mods': extras}

    def s_compare(self):
        res = compare_dbs(self.base.test_db, self.db)
        p = self.out / f'compare-{self.base.tag}.json'
        p.write_text(json.dumps(res, indent=1) + '\n', encoding='utf-8')
        self.identical = res['identical']
        return {'report': str(p), 'identical': res['identical'], 'tables_compared': res['tables_compared'],
                'identical_tables': res['identical_tables'], 'tables_differ': [d['table'] for d in res['differ']],
                'only_in_base': res['only_in_a'], 'only_in_built': res['only_in_b']}

    def s_descriptor(self):
        same = self.tag == self.base.tag
        use_built = not same or getattr(self, 'identical', False)
        test_db = self.db if use_built else self.base.test_db
        d = {
            'format': descriptor.FORMAT, 'tag': self.tag, 'upstream_commit': self.commit, 'mods': self.mods,
            'game_dir': self.game_dir.as_posix(),
            'test_db': test_db.as_posix(), 'test_db_sha256': sha256(test_db),
            'test_db_source': ('built: tools/release/stages/testdata.py, c5h dbbuild of the tag\'s mods ('
                               + ', '.join(m['folder'] for m in self.mods + self.tag_db_mods)
                               + (' and extra mods: ' + ', '.join(m['folder'] for m in self.db_extras)
                                  if self.db_extras else '') + ')'
                               if use_built else 'pinned: the base descriptor\'s test_db (the build did not reproduce it; '
                               f'compare-{self.base.tag}.json)'),
            'loc_db': self.base.loc_db.as_posix(),
            'reference_saves': {k: {'path': s.path.as_posix(), 'sha256': s.sha256, 'turn': s.turn}
                                for k, s in self.base.reference_saves.items()},
            'reference_counts': dict(self.base.reference_counts) if same else {},
            'economic_strategies': self.strategies,
        }
        if self.base.loc_db_sha256:
            d['loc_db_sha256'] = self.base.loc_db_sha256
        if self.player_dll:
            d['player_dll_sha256'] = sha256(self.player_dll)
            stock = self.modpack / 'Mods' / self.player_dll.parent.name / self.player_dll.name
            d['stock_x86_dll'], d['stock_x86_dll_sha256'] = stock.as_posix(), sha256(stock)
        # issue 3512 (the pack stage of --major-civs 43 checks it): the tag's own 43-civ player DLL, else the base's
        # value when the base is this release
        dll43 = self.tag_file_sha256(PLAYER_DLL_43)
        if dll43 or (same and self.base.player_dll_43_sha256):
            d['player_dll_43_sha256'] = dll43 or self.base.player_dll_43_sha256
        if same and self.base.reference_counts_pins:
            d['reference_counts_pins'] = self.base.reference_counts_pins
        # issue 0464 (MC #3827): the release's shell configuration (2602) goes with the release; absent stays absent
        if getattr(self.base, 'shell_config', None):
            d['shell_config'] = self.base.shell_config
        # ticket 0574: an i3 release's option set goes with it too (the base's raw field: absent stays absent)
        if 'options' in getattr(self.base, 'raw', {}):
            d['options'] = self.base.raw['options']
        if 'purity_allowed_drops' in getattr(self.base, 'raw', {}):   # ticket 0574 (MC #7046)
            d['purity_allowed_drops'] = self.base.raw['purity_allowed_drops']
        d['notes'] = (f'candidate descriptor written by the testdata stage {now()}; loc_db is the base release\'s '
                      f'({self.base.tag}): the localization is not rebuilt per release yet (release-driver.md, 3501)')
        p = self.out / f'{self.tag}.json'
        descriptor.parse(json.loads(json.dumps(d)), str(p), expect_tag=self.tag)  # validate before writing
        p.write_text(json.dumps(d, indent=1) + '\n', encoding='utf-8')
        return {'descriptor': str(p), 'test_db': d['test_db']}

    def go(self) -> dict:
        self.out.mkdir(parents=True, exist_ok=True)
        steps = [('commit', self.s_commit), ('mods', self.s_mods), ('game', self.s_game), ('config', self.s_config),
                 ('snapshot', self.s_snapshot), ('dbbuild', self.s_dbbuild)]
        if self.compare:
            steps.append(('compare', self.s_compare))
        steps.append(('descriptor', self.s_descriptor))
        try:
            self.core_sha256 = check_core(self.core, self.core_pins)   # ticket 0612: before any step
            for name, fn in steps:
                self.step(name, fn)
        except Stopped as e:
            self.record.update(status='stop', ended=now(), stop=str(e))
            self.save_record()
            return {'status': 'stop', 'outputs': {}, 'stop': {'what': str(e), 'cl_step': 2,
                                                              'unblock': f'remove {self.out / "STOP"} and run again'}}
        except (StageError, OSError, sqlite3.Error, ET.ParseError, tarfile.TarError, descriptor.DescriptorError,
                _modinfo.ModinfoError, UnicodeDecodeError) as e:
            self.record.update(status='failed', ended=now(), error=f'{type(e).__name__}: {e}')
            self.save_record()
            return {'status': 'failed', 'outputs': {}, 'error': f'{type(e).__name__}: {e}'}
        self.record.update(status='ok', ended=now())
        self.save_record()
        outs = {'descriptor': str(self.out / f'{self.tag}.json'), 'mods_dir': str(self.out / 'mods'),
                'modinfo_regenerated': sorted(f for f, r in self.modinfo_regen.items() if r['changed']),
                'game_dir': str(self.game_dir), 'test_db': str(self.db), 'record': str(self.out / 'testdata.json')}
        if self.compare:
            outs['identical_to_base'] = self.identical
        return {'status': 'ok', 'outputs': outs}


def inputs(ctx) -> dict:
    """The driver's inputs of the stage (issue 3512): the base descriptor (its tag and content) and the revision, so a
    run that changes its --base-release or --descriptor writes the candidate again."""
    base = getattr(ctx, 'descriptor', None)
    raw = getattr(base, 'raw', None) or {}
    return {'base': getattr(base, 'tag', None),
            'base_sha256': hashlib.sha256(json.dumps(raw, sort_keys=True).encode('utf-8')).hexdigest() if raw else None,
            'ref': getattr(ctx, 'ref', None)}


def run(ctx) -> dict:
    """The driver's entry point (tools/release/README.md section 2)."""
    base = getattr(ctx, 'descriptor', None) or descriptor.load()
    runner = getattr(ctx, 'run', None)

    def heavy(name, cmd, cwd=None, env=None):
        # the driver's runner with a name: logged in <work>/logs/testdata-<name>.log, under quietlock.py run; a gate
        # refusal raises the driver's GateRefused (exit 3), the stop file its StopRequested
        r = ctx.run(cmd, name=name, cwd=cwd, env=env, what=f'{ctx.who} take_release {ctx.tag} testdata {name}')
        return subprocess.CompletedProcess(cmd, r.rc, r.text, '')
    cfg = getattr(ctx, 'config', None) or {}
    # test hooks (issue 3512's driver test runs this stage on a fake c5h and core database): config c5h, core_db
    st = Stage(ctx.tag, Path(ctx.work) / 'testdata', Path(getattr(ctx, 'fork', None) or FORK), base, runner=runner,
               heavy=heavy if runner else None, ref=getattr(ctx, 'ref', None),
               core=cfg.get('core_db') or CORE_DB, c5h=Path(cfg.get('c5h') or C5H), core_pins=cfg.get('core_db_pins'))
    return st.go()


def quietlock_heavy(name, cmd, cwd=None, env=None):
    """The command line's heavy runner: each heavy step under perf/quietlock/quietlock.py run (COMMON 16)."""
    ql = [sys.executable, '-I', str(HERE.parents[2] / 'perf' / 'quietlock' / 'quietlock.py'), 'run', '--who',
          os.environ.get('C5_JOB_OWNER', '3500'), '--what', f'testdata {name}', '--']
    return subprocess.run(ql + [str(c) for c in cmd], cwd=cwd, env=env, capture_output=True, text=True,
                          errors='replace')


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tag', required=True, help='release tag in the fork (e.g. Release-5.4.8)')
    ap.add_argument('--out', type=Path, required=True, help='output folder (the driver: <work>/testdata)')
    ap.add_argument('--fork', type=Path, default=FORK, help=f'fork clone holding the tag (default {FORK})')
    ap.add_argument('--base', help='release whose descriptor gives the game folder, mod set and saves (default: current)')
    ap.add_argument('--core', type=Path, default=CORE_DB, help='the pinned Civ5CoreDatabase.db to copy (read only; its SHA-256 must be in CORE_DB_PINS)')
    ap.add_argument('--dll', type=Path, help="post-processing DLL (default: the tag's (1) Community Patch DLL)")
    ap.add_argument('--extra-mod', type=Path, action='append', default=[],
                    help='a further mod folder activated in the test database only (copied; repeatable; e.g. the '
                         "toolkit modpack's Squads or Quick Turns: release-driver.md section 3501)")
    ap.add_argument('--c5h', type=Path, default=C5H, help='civ5-headless folder (c5h.cmd; used as a tool, never written)')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--compare', dest='compare', action='store_true', default=None)
    g.add_argument('--no-compare', dest='compare', action='store_false')
    a = ap.parse_args(argv)
    try:
        base = descriptor.load(a.base)
    except descriptor.DescriptorError as e:
        print(f'testdata: {e}', file=sys.stderr)
        return 2
    try:
        check_core(a.core)
    except StageError as e:
        print(f'testdata: {e}', file=sys.stderr)
        return 2
    res = Stage(a.tag, a.out, a.fork, base, core=a.core, dll=a.dll, compare=a.compare, c5h=a.c5h,
                extra_mods=a.extra_mod, heavy=quietlock_heavy).go()
    print(json.dumps(res, indent=1))
    if res['status'] == 'stop':
        print(f"testdata: STOP testdata (CL step 2): {res['stop']['what']}; to unblock: {res['stop']['unblock']}")
        return STOPPED
    return 0 if res['status'] == 'ok' else 1


if __name__ == '__main__':
    if sys.argv[1:2] == ['--copy-game']:  # the game-folder copy as its own process (Stage.s_game with a heavy runner)
        copy_game(Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4:])
        sys.exit(0)
    sys.exit(main())
