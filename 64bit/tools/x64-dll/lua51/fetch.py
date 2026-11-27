"""Locate, verify and unpack stock Lua 5.1.4 (issues 1005, 1405).

The game's own Lua is 5.1.4: `lua51_Win32.dll` in the install carries the string
"$Lua: Lua 5.1.4 Copyright (C) 1994-2008 Lua.org, PUC-Rio $" (read 2026-11-10).

The tarball is never committed and never stored in the repository. It is found (and only if missing,
downloaded) by `tools/x64-dll/thirdparty.py` from the `[lua]` entry of `deps/third-party.lock`, which holds
the URL and the SHA-256 (the one lua.org publishes): an explicit `--lua-tarball` path first, then the cache
root `$CV64_DEPS_CACHE` (default %LOCALAPPDATA%\\Temp\\claude\\cv64-deps, outside the tree),
then a download unless `$CV64_OFFLINE=1`. The verified tarball is unpacked under the caller's output
directory (`<out>/lua-5.1.4/`), never next to this script.

Usage:
    python fetch.py --out DIR [--lua-tarball PATH] [--offline]   prints the path of the unpacked `src` directory
As a module:
    import fetch; src_dir = fetch.ensure_lua(out_dir, tarball=None, offline=None)   # raises thirdparty.ThirdPartyError
"""
import argparse
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import thirdparty  # noqa: E402  tools/x64-dll/thirdparty.py

_LOCK = thirdparty.read_lock()['lua']
LUA_VERSION = _LOCK['version']
LUA_URL = _LOCK['url']
LUA_SHA256 = _LOCK['sha256'].lower()


def ensure_lua(out, tarball=None, offline=None):
    """Return the absolute path of <out>/lua-5.1.4/src, unpacked from the verified tarball."""
    tgz = thirdparty.ensure('lua', explicit=tarball, offline=offline)
    out = Path(out).resolve()
    root = out / f'lua-{LUA_VERSION}'
    stamp = root / ('.verified-' + LUA_SHA256[:16])
    if not stamp.exists():
        out.mkdir(parents=True, exist_ok=True)
        with tarfile.open(tgz, 'r:gz') as tf:
            # 'data' filter: no absolute paths, no links out of the tree, no special files.
            tf.extractall(out, filter='data')
        stamp.write_text(LUA_SHA256 + '\n')
    src = root / 'src'
    if not (src / 'lapi.c').exists():
        raise thirdparty.ThirdPartyError(f'unpacked tree at {root} has no src/lapi.c')
    return src


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True, help='directory to unpack into')
    ap.add_argument('--lua-tarball', help='explicit path of lua-5.1.4.tar.gz (checked against deps/third-party.lock)')
    ap.add_argument('--offline', action='store_true', help='never download (same as CV64_OFFLINE=1)')
    a = ap.parse_args()
    try:
        print(ensure_lua(a.out, a.lua_tarball, True if a.offline else None))
    except thirdparty.ThirdPartyError as e:
        print(f'fetch: FAILED: {e}', file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main()
