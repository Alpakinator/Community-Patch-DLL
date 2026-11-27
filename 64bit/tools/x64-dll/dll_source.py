"""Where the DLL source is: the one setting every build tool reads when it is not given --src.

The DLL source is the tree that holds CvGameCoreDLL_Expansion2/, CvGameCoreDLLUtil/, build_vp_clang.py and the
rest of the DLL repository. Its location is one setting, looked up in this order:

1. the environment variable VP_DLL_SRC;
2. `src = <path>` in deps/dll-source.ini, a file of this repository (absent where the tools are used without it);
3. the upstream layout: these tools sit in a folder (`64bit/`) at the top of the DLL repository, so the source is
   `..` seen from that folder, where `../CvGameCoreDLL_Expansion2` is.

A relative value is taken from the folder that holds tools/ (the repository root). The setting says only where the
source is; which commit it must be at is a separate question that tools/x64-dll/build.py's resolve_src answers
against deps/dll.lock.

A second key of the same file names the fork clone (ticket 4709): the git repository that the release and save-identity
tools read for release tags, ancestry checks and `git archive` of a commit (tools/release/stages/testdata.py,
tools/d10/run_d10.py), never as a source tree. It may be a different checkout from the source (here it is: the source
is a work clone kept at the x64 pin, the fork clone the one that fetches the release tags). Looked up in this order:

1. the environment variable VP_DLL_FORK;
2. `fork = <path>` in deps/dll-source.ini;
3. the DLL source setting above (upstream, the DLL repository is both the source and the clone with the tags).

    python tools/x64-dll/dll_source.py      prints both locations and which rule gave each
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]   # the folder that holds tools/: `64bit/` in the upstream layout
ENV_VAR = 'VP_DLL_SRC'
SETTING_FILE = ROOT / 'deps' / 'dll-source.ini'
SETTING_KEY = 'src'
UPSTREAM_DEFAULT = '..'
FORK_ENV_VAR = 'VP_DLL_FORK'
FORK_KEY = 'fork'


class SettingError(Exception):
    pass


def read_setting_file(path: Path = SETTING_FILE, key: str = SETTING_KEY):
    """The value of `key` (default `src`) in the setting file, or None when the file is absent or has no such line.

    Format as deps/dll.lock: `key = value` lines, `#` comments, blank lines ignored."""
    if not path.is_file():
        return None
    for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        s = line.strip()
        if not s or s.startswith('#'):
            continue
        k, sep, val = s.partition('=')
        if not sep:
            raise SettingError(f'{path}:{n}: not a `key = value` line: {s}')
        if k.strip() == key:
            val = val.strip()
            if not val:
                raise SettingError(f'{path}:{n}: `{key} =` has no value')
            return val
    return None


def setting(environ=None, setting_file: Path = SETTING_FILE) -> tuple:
    """(value as written, origin): origin names the environment variable, the setting file or the upstream layout."""
    env = os.environ if environ is None else environ
    v = (env.get(ENV_VAR) or '').strip()
    if v:
        return v, f'environment variable {ENV_VAR}'
    v = read_setting_file(setting_file)
    if v is not None:
        return v, _file_name(setting_file)
    return UPSTREAM_DEFAULT, 'the upstream layout (no setting given)'


def _file_name(setting_file: Path) -> str:
    return setting_file.resolve().relative_to(ROOT).as_posix() if _under(setting_file, ROOT) else str(setting_file)


def fork_setting(environ=None, setting_file: Path = SETTING_FILE) -> tuple:
    """(value as written, origin) of the fork clone: VP_DLL_FORK, else `fork =` in the file, else the source setting."""
    env = os.environ if environ is None else environ
    v = (env.get(FORK_ENV_VAR) or '').strip()
    if v:
        return v, f'environment variable {FORK_ENV_VAR}'
    v = read_setting_file(setting_file, FORK_KEY)
    if v is not None:
        return v, f'{_file_name(setting_file)} `{FORK_KEY}`'
    v, where = setting(environ, setting_file)
    return v, f'the DLL source setting ({where})'


def fork_clone(environ=None, setting_file: Path = SETTING_FILE) -> Path:
    """The fork clone this setting gives, absolute (not checked: the tools' git commands fail on a wrong one)."""
    return resolve(fork_setting(environ, setting_file)[0])


def fork_origin(environ=None, setting_file: Path = SETTING_FILE) -> str:
    return fork_setting(environ, setting_file)[1]


def _under(p: Path, root: Path) -> bool:
    try:
        p.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def resolve(value: str, root: Path = ROOT) -> Path:
    """The setting's value as an absolute path: a relative value is taken from `root`."""
    p = Path(value)
    return (p if p.is_absolute() else root / p).resolve()


def dll_source(environ=None, setting_file: Path = SETTING_FILE) -> Path:
    """The DLL source location this setting gives (not checked: resolve_src in build.py checks it)."""
    return resolve(setting(environ, setting_file)[0])


def origin(environ=None, setting_file: Path = SETTING_FILE) -> str:
    return setting(environ, setting_file)[1]


def main(argv=None) -> int:
    try:
        value, where = setting()
        fvalue, fwhere = fork_setting()
    except SettingError as e:
        print(f'dll_source: FAILED: {e}', file=sys.stderr)
        return 1
    print(f'{resolve(value)}\t(from {where}; value {value!r})')
    print(f'fork clone: {resolve(fvalue)}\t(from {fwhere}; value {fvalue!r})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
