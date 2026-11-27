"""Repeatability check of the x64 DLL's link inputs (issue 0131).

Two release builds of one source and tool commit in two folders gave two DLLs although every LTO bitcode object and
the link command were byte-equal (one-off 9045, research/03-dll-engine-boundary/upstream-fix-batch-1.md 8c). The inputs
that differed were the closed-library stub libraries: clang-cl writes the compile time into the COFF header
TimeDateStamp of every native object unless it is given /Brepro, and the stubs are native objects archived into .lib
files. This module reads a link's response file and reports every link input of the build's own folder that still
carries such a time stamp. build.py prints its one-line verdict after every link ("repeatability check: ...").

    python -I repeatcheck.py <out>/link.rsp        (the same line, for an existing build folder)
"""
from __future__ import annotations

import shlex
import struct
import sys
from pathlib import Path

ARCH_MAGIC = b'!<arch>\n'
BITCODE = (b'BC\xc0\xde', b'\xde\xc0\x17\x0b')
MACHINES = (0x8664, 0x14C, 0xAA64)


def member_stamp(body: bytes):
    """(kind, TimeDateStamp) of one object file's bytes: kind is 'bitcode', 'coff', 'bigobj', 'import' or None."""
    if body[:4] in BITCODE:
        return 'bitcode', None
    if len(body) >= 20 and struct.unpack_from('<H', body, 0)[0] in MACHINES:
        return 'coff', struct.unpack_from('<I', body, 4)[0]
    if len(body) >= 12 and body[:4] == b'\0\0\xff\xff':
        ver = struct.unpack_from('<H', body, 4)[0]
        return ('import' if ver == 0 else 'bigobj'), struct.unpack_from('<I', body, 8)[0]
    return None, None


def archive_members(data: bytes):
    """(name, bytes) of every member of a COFF archive, without the linker members and the long-name table."""
    off, longnames, out = 8, b'', []
    while off + 60 <= len(data):
        hdr = data[off:off + 60]
        name = hdr[:16].decode('latin-1').rstrip()
        size = int(hdr[48:58].decode('latin-1').strip() or 0)
        body = data[off + 60:off + 60 + size]
        if name == '//':
            longnames = body
        elif name not in ('/', '/SYM64/'):
            if name.startswith('/') and name[1:].isdigit():
                i = int(name[1:])
                end = longnames.find(b'\0', i)
                name = longnames[i:end if end >= 0 else len(longnames)].decode('latin-1').rstrip('/\n')
            out.append((name.rstrip('/'), body))
        off += 60 + size + (size & 1)
    return out


def stamped(path: Path):
    """(members, [(member, kind, stamp)] with a non-zero COFF time stamp) of one object or library file.
    Short import members are not counted as stamped: lld-link writes them with stamp 0 under /Brepro and the
    Windows SDK's are fixed files."""
    data = path.read_bytes()
    items = archive_members(data) if data[:8] == ARCH_MAGIC else [(path.name, data)]
    bad = []
    for name, body in items:
        kind, ts = member_stamp(body)
        if kind in ('coff', 'bigobj') and ts:
            bad.append((name, kind, ts))
    return len(items), bad


def rsp_inputs(rsp: Path):
    """The object and library files a response file names that exist relative to its folder (the build's own
    inputs; system libraries named without a folder are found on LIB and are not the build's)."""
    base = rsp.parent
    out = []
    for tok in shlex.split(rsp.read_text(encoding='utf-8'), posix=False):
        tok = tok.strip('"')
        if tok.startswith('/') or not tok.lower().endswith(('.obj', '.lib', '.o', '.a')):
            continue
        p = base / tok
        if p.is_file():
            out.append(p)
    return out


def check(rsp: Path) -> dict:
    files = rsp_inputs(rsp)
    members, bad = 0, []
    for f in files:
        n, b = stamped(f)
        members += n
        bad += [(f.name, m, k, ts) for m, k, ts in b]
    return {'inputs': len(files), 'members': members, 'stamped': len(bad), 'examples': bad[:5],
            'ok': not bad and bool(files)}


def line(res: dict, dll_sha256: str | None = None) -> str:
    h = f'; DLL sha256 {dll_sha256}' if dll_sha256 else ''
    if res['ok']:
        return (f"repeatability check: OK: {res['inputs']} link inputs of this build ({res['members']} objects and "
                f"library members), none carries a time stamp{h}")
    if not res['inputs']:
        return f'repeatability check: NOT REPEATABLE: no link input of this build found in the response file{h}'
    ex = ', '.join(f'{f}({m}) {ts}' for f, m, k, ts in res['examples'])
    return (f"repeatability check: NOT REPEATABLE: {res['stamped']} of {res['members']} objects and library members of this "
            f"build carry a COFF time stamp (e.g. {ex}); a build in another folder or at another time can give other "
            f"DLL bytes (issue 0131){h}")


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: python -I repeatcheck.py <out>/link.rsp')
    r = check(Path(sys.argv[1]))
    print(line(r))
    sys.exit(0 if r['ok'] else 1)
