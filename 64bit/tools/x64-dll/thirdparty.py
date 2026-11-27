"""Locate, verify and (only if needed) download the third-party archives listed in deps/third-party.lock (epic 1400).

Lookup order for an entry NAME (`lua`, `sqlite`, `zlib`):
  1. an explicit path (the build script's `--lua-tarball` / `--sqlite-zip` / `--zlib-tarball`);
  2. the cache root: $CV64_DEPS_CACHE, default %LOCALAPPDATA%\\Temp\\claude\\cv64-deps (outside the repository; on the
     workstation inside the Defender exclusion), file name from the lock;
  3. a download from the lock's URL into the cache root (Windows curl.exe, then urllib; certificates checked),
     unless `offline=True` or $CV64_OFFLINE=1.
The SHA-256 from the lock is checked in every case. Any failure raises ThirdPartyError with one line that names the
URL, the expected hash and where to put the file; build scripts turn it into a non-zero exit.

As a script:  python thirdparty.py [NAME ...] [--offline]   verifies (and fetches) and prints the paths.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LOCK = REPO / 'deps' / 'third-party.lock'
# %LOCALAPPDATA%\Temp\claude\cv64-deps (ticket 4711: written from the environment, the same folder as before)
DEFAULT_CACHE = (Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData' / 'Local')
                 / 'Temp' / 'claude' / 'cv64-deps')
CURL = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32' / 'curl.exe'
# The build-script option that names each archive explicitly (only used in error messages).
OPTION = {'lua': '--lua-tarball (build_stubs.py, build_all.py)', 'sqlite': '--sqlite-zip (build_db.py, build_all.py)',
          'zlib': '--zlib-tarball (server/build_server.py)'}


class ThirdPartyError(Exception):
    pass


def read_lock(path=LOCK):
    entries, cur, key = {}, None, None
    for raw in path.read_text(encoding='utf-8').splitlines():
        line = raw.rstrip()
        if not line or line.lstrip().startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            cur = entries.setdefault(line[1:-1], {})
            continue
        if raw[:1].isspace() and cur is not None and key:  # continuation line
            cur[key] += ' ' + line.strip()
            continue
        k, sep, v = line.partition('=')
        if sep and cur is not None:
            key = k.strip()
            cur[key] = v.strip()
    return entries


def cache_root():
    return Path(os.environ.get('CV64_DEPS_CACHE') or DEFAULT_CACHE)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 16), b''):
            h.update(block)
    return h.hexdigest()


def _download(url, dest, attempts=4):
    tmp = dest.with_name(dest.name + '.part')
    last = None
    for i in range(attempts):
        try:
            if CURL.exists() and i % 2 == 0:
                r = subprocess.run([str(CURL), '-sS', '-f', '-L', '--connect-timeout', '20', '--max-time', '180',
                                    '-o', str(tmp), url], capture_output=True, text=True)
                if r.returncode != 0:
                    raise OSError(f'curl exit {r.returncode}: {r.stderr.strip()}')
            else:
                with urllib.request.urlopen(url, timeout=60) as resp, open(tmp, 'wb') as f:
                    f.write(resp.read())
            tmp.replace(dest)
            return
        except Exception as e:  # noqa: BLE001 - report the last one
            last = e
            time.sleep(2 * (i + 1))
    raise ThirdPartyError(f'download failed: {last}')


def ensure(name, explicit=None, offline=None):
    """Return the verified path of archive NAME. Raises ThirdPartyError."""
    entries = read_lock()
    if name not in entries:
        raise ThirdPartyError(f'{name}: no [{name}] entry in {LOCK}')
    e = entries[name]
    want = e['sha256'].lower()
    opt = OPTION.get(name, "the build script's option")
    hint = (f'{name} {e["version"]}: get {e["url"]} (sha256 {want}) and pass it with {opt}, '
            f'or put it at {cache_root() / e["file"]}')
    if explicit:
        p = Path(explicit)
        if not p.is_file():
            raise ThirdPartyError(f'{p} does not exist. {hint}')
        got = sha256(p)
        if got != want:
            raise ThirdPartyError(f'{p}: sha256 {got} != locked {want}. {hint}')
        return p
    p = cache_root() / e['file']
    if p.is_file():
        got = sha256(p)
        if got == want:
            return p
        raise ThirdPartyError(f'{p}: sha256 {got} != locked {want} (delete it to re-download). {hint}')
    if offline is None:
        offline = os.environ.get('CV64_OFFLINE') == '1'
    if offline:
        raise ThirdPartyError(f'{p} missing and offline. {hint}')
    p.parent.mkdir(parents=True, exist_ok=True)
    try:
        _download(e['url'], p)
    except ThirdPartyError as err:
        raise ThirdPartyError(f'{err}. {hint}') from None
    got = sha256(p)
    if got != want:
        p.unlink()
        raise ThirdPartyError(f'downloaded {e["url"]}: sha256 {got} != locked {want}. {hint}')
    return p


def main():
    # argparse (issue 1508): --help and unknown options exit before anything is looked up or downloaded.
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', metavar='NAME', help='lock entries (default: all of deps/third-party.lock)')
    ap.add_argument('--offline', action='store_true', help='never download (also $CV64_OFFLINE=1)')
    a = ap.parse_args()
    names = a.names or sorted(read_lock())
    rc = 0
    for n in names:
        try:
            print(f'{n}: {ensure(n, offline=True if a.offline else None)}')
        except ThirdPartyError as err:
            print(f'error: {err}', file=sys.stderr)
            rc = 1
    sys.exit(rc)


if __name__ == '__main__':
    main()
