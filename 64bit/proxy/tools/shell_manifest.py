"""Write or check shell_composite.sha256, the manifest that pairs the composite DLL with one build of the proxy (issue 3305).

    python proxy/tools/shell_manifest.py write --proxy <CvGameCore_Expansion2.dll> --composite <shell_composite.dll> [--out FILE]
    python proxy/tools/shell_manifest.py check <folder>

The proxy (proxy/src/proxy_shell.cpp) loads <its folder>\\shell_composite.dll only when VP64Options.ini [Renderer]
ServerRenderer = 1 and <its folder>\\shell_composite.sha256 names the proxy's own SHA-256 and the composite's:

    # comment
    proxy <64 hex digits>
    composite <64 hex digits>

`write` puts the manifest beside the composite (or at --out); run it when the patch's x86 files are packaged or provisioned
into an instance, after the proxy and the composite are in their final form. `check` verifies a folder that holds the
proxy (CvGameCore_Expansion2.dll), shell_composite.dll and the manifest the way the proxy will; exit 0 when they match,
1 otherwise. Python standard library only.
"""
import argparse
import hashlib
import sys
from pathlib import Path

PROXY_NAME = 'CvGameCore_Expansion2.dll'
COMPOSITE_NAME = 'shell_composite.dll'
MANIFEST_NAME = 'shell_composite.sha256'


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def manifest_text(proxy_sha, composite_sha, note=''):
    head = '# shell_composite.sha256 - written by proxy/tools/shell_manifest.py (issue 3305)%s\n' % (
        ('; ' + note) if note else '')
    return head + 'proxy %s\ncomposite %s\n' % (proxy_sha, composite_sha)


def write(proxy, composite, out=None):
    out = Path(out) if out else Path(composite).with_name(MANIFEST_NAME)
    text = manifest_text(sha256(proxy), sha256(composite))
    out.write_bytes(text.encode('ascii'))
    return out, text


def parse(text):
    """{'proxy': hex, 'composite': hex} or raises ValueError (the same rules as the proxy's parser)."""
    got = {}
    for ln in text.splitlines():
        s = ln.strip(' \t')
        if not s or s.startswith('#'):
            continue
        parts = s.split(None, 1)
        if parts[0] not in ('proxy', 'composite'):
            raise ValueError('a line is neither a comment, "proxy <sha256>" nor "composite <sha256>"')
        if parts[0] in got:
            raise ValueError('a hash is named twice')
        v = parts[1].strip() if len(parts) > 1 else ''
        if len(v) != 64 or any(c not in '0123456789abcdefABCDEF' for c in v):
            raise ValueError('a hash is not 64 hex digits')
        got[parts[0]] = v.lower()
    for k in ('proxy', 'composite'):
        if k not in got:
            raise ValueError('no "%s <sha256>" line' % k)
    return got


def check(folder):
    folder = Path(folder)
    m = parse((folder / MANIFEST_NAME).read_text(encoding='ascii'))
    problems = []
    if sha256(folder / PROXY_NAME) != m['proxy']:
        problems.append('the manifest belongs to another build of the proxy')
    if sha256(folder / COMPOSITE_NAME) != m['composite']:
        problems.append('the SHA-256 of shell_composite.dll differs from the manifest')
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    w = sub.add_parser('write')
    w.add_argument('--proxy', required=True, type=Path)
    w.add_argument('--composite', required=True, type=Path)
    w.add_argument('--out', type=Path)
    c = sub.add_parser('check')
    c.add_argument('folder', type=Path)
    a = ap.parse_args()
    if a.cmd == 'write':
        out, text = write(a.proxy, a.composite, a.out)
        print('shell_manifest: wrote %s' % out)
        print(text, end='')
        return 0
    try:
        problems = check(a.folder)
    except (OSError, ValueError) as e:
        print('shell_manifest: FAIL: %s' % e)
        return 1
    for p in problems:
        print('shell_manifest: FAIL: %s' % p)
    if not problems:
        print('shell_manifest: OK: %s matches %s and %s' % (MANIFEST_NAME, PROXY_NAME, COMPOSITE_NAME))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
