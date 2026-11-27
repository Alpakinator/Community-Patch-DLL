"""This machine's paths for the build and release tools: one setting each, never a user folder written in the code.

Ticket 4711 (epic 0468, D174: the export plan's E1a). Each setting is looked up in this order:

1. its environment variable (table below);
2. `<key> = <value>` in the settings file: the file named by the environment variable C5_SETTINGS_FILE, else
   deps/local-settings.ini of this repository (private: deps/ is not exported, apart from deps/third-party.lock, and
   the file names this machine's folders; it is absent where the tools are used without it);
3. the default below, where there is one.

    key              environment variable    default                          read by
    pins_dir         C5_PINS_DIR             none: required (a guard)         tools/release/take_release.py, stages/build.py
    clones_dir       C5_CLONES_DIR           the repository's parent when it  tools/release/take_release.py (--fork guard)
                                             is named GitHub, else none
    fork_url         C5_FORK_URL             none: the lock stage fails       tools/release/stages/lock.py
    c5h              C5_C5H                  <home>/Documents/civ5-headless   stages/testdata.py, stages/wholegames.py
    core_db          C5_CORE_DB              none: the testdata stage refuses stages/testdata.py
                                             (ticket 0612: a pinned copy named by SHA-256 in testdata.CORE_DB_PINS;
                                             the game's live cache was the default before)
    render_game_dir  C5_RENDER_GAME_DIR      none: the render tests skip      render/build.py --test
    render_exe       C5_RENDER_EXE           none: the render tests skip      render/build.py --test
    exe_tables       C5_EXE_TABLES           none: the builds fail            tools/exebuild/ (table.py, gen_table.py,
                                                                              emit_header.py; the four build scripts)
    pgo_dir          C5_PGO_DIR              none: a release-pgo build        tools/x64-dll/build.py lock_profile
                                             refuses, naming this setting     (deps/dll.lock `profile-file =
                                                                              <pgo_dir>/<name>`, ticket 0774)

pins_dir is the folder under which the MC lays out the pinned builds; clones_dir is the folder of the user's own
clones. take_release.py never writes under either (its FORBIDDEN_WORK and --fork guards): without the setting it
refuses to run (exit 2) rather than guard less. A relative value is taken from the repository root (the folder that
holds tools/); `%VAR%`, `$VAR` and `~` are expanded.

A lock file names a machine folder through a setting, never by its path (ticket 0774; deps/dll.lock is exported):
`resolve_placeholder('<pgo_dir>/x.profdata')` gives the setting's folder joined with the rest (`%VAR%` and `$VAR` are
expanded too), and SettingError naming the setting when it is not set.

    python -I tools/release/settings.py      prints every setting, its value and where it came from
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FILE_ENV = 'C5_SETTINGS_FILE'
DEFAULT_FILE = REPO / 'deps' / 'local-settings.ini'

ENV = {
    'pins_dir': 'C5_PINS_DIR',
    'clones_dir': 'C5_CLONES_DIR',
    'fork_url': 'C5_FORK_URL',
    'c5h': 'C5_C5H',
    'core_db': 'C5_CORE_DB',
    'render_game_dir': 'C5_RENDER_GAME_DIR',
    'render_exe': 'C5_RENDER_EXE',
    'exe_tables': 'C5_EXE_TABLES',   # ticket 4960 (epic 0590, D202): the local per-build EXE address tables
    'pgo_dir': 'C5_PGO_DIR',         # ticket 0774: the folder of the locked PGO profiles (deps/dll.lock profile-file)
}
NOT_PATHS = {'fork_url'}


class SettingError(Exception):
    pass


def _defaults() -> dict:
    home = Path.home()
    d = {'c5h': home / 'Documents' / 'civ5-headless'}
    if REPO.parent.name == 'GitHub':
        d['clones_dir'] = REPO.parent
    return d


def settings_file(environ=None) -> Path:
    env = os.environ if environ is None else environ
    v = (env.get(FILE_ENV) or '').strip()
    return Path(os.path.expandvars(v)).expanduser() if v else DEFAULT_FILE


def read_file(path: Path) -> dict:
    """`key = value` lines of the settings file ({} when it is absent); `#` comments and blank lines are skipped."""
    if not path.is_file():
        return {}
    out = {}
    for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        k, sep, v = s.partition('=')
        k, v = k.strip(), v.strip()
        if not sep or not v:
            raise SettingError(f'{path}:{n}: not a `key = value` line')
        if k not in ENV:
            raise SettingError(f'{path}:{n}: unknown key {k!r} (known: {", ".join(ENV)})')
        out[k] = v
    return out


def lookup(key: str, environ=None) -> tuple:
    """(value, origin): the value is a Path (a str for fork_url), None when the setting is not given and has no default."""
    if key not in ENV:
        raise KeyError(key)
    env = os.environ if environ is None else environ
    v = (env.get(ENV[key]) or '').strip()
    origin = f'environment variable {ENV[key]}'
    if not v:
        f = settings_file(env)
        v = read_file(f).get(key)
        origin = f'{_name(f)} `{key}`'
    if not v:
        d = _defaults().get(key)
        return d, ('the default' if d is not None else 'not set')
    if key in NOT_PATHS:
        return v, origin
    p = Path(os.path.expandvars(v)).expanduser()
    return (p if p.is_absolute() else REPO / p), origin


def get(key: str, environ=None):
    return lookup(key, environ)[0]


def require(key: str, why: str, environ=None):
    """The setting's value, or SettingError naming the environment variable and the settings key to set."""
    v = get(key, environ)
    if v is None:
        raise SettingError(f'setting {key} is not set ({why}): set the environment variable {ENV[key]} or '
                           f'`{key} = <value>` in {_name(settings_file(environ))} (tools/release/settings.py)')
    return v


_PLACEHOLDER = re.compile(r'^<([a-z_]+)>(?:[/\\](.*))?$')


def resolve_placeholder(value: str, why: str = 'a lock file names it', environ=None) -> Path:
    """A path written in a lock file without a machine folder (ticket 0774): `<key>/rest` = setting `key` (one of ENV,
    a path setting) joined with rest; otherwise the value with `%VAR%`, `$VAR` and `~` expanded. SettingError when the
    placeholder names an unknown key or a setting that is not set, or when an environment variable stays unexpanded."""
    v = value.strip()
    m = _PLACEHOLDER.match(v)
    if m:
        key, rest = m.group(1), (m.group(2) or '')
        if key not in ENV or key in NOT_PATHS:
            raise SettingError(f'{v}: <{key}> is not a path setting (known: '
                               f'{", ".join(k for k in ENV if k not in NOT_PATHS)})')
        base = require(key, why, environ)
        return base / rest if rest else base
    env = os.environ if environ is None else environ
    out = re.sub(r'%([A-Za-z_][A-Za-z0-9_]*)%', lambda x: env.get(x.group(1), x.group(0)), v)
    out = re.sub(r'\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?', lambda x: env.get(x.group(1), x.group(0)), out)
    if re.search(r'%[A-Za-z_][A-Za-z0-9_]*%|\$\{?[A-Za-z_]', out):
        raise SettingError(f'{v}: an environment variable in it is not set ({why})')
    return Path(out).expanduser()


def _name(p: Path) -> str:
    try:
        return p.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return str(p)


def main(argv=None) -> int:
    try:
        for k in ENV:
            v, where = lookup(k)
            print(f'{k}\t{v if v is not None else "(not set)"}\t(from {where})')
    except SettingError as e:
        print(f'settings: FAILED: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
