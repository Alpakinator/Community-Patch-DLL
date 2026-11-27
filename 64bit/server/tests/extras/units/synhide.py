"""A synthetic "after a live plot change" scene bundle for FixAirBases (epic 0455, issue 4985; 2026-11-20).

A copy of a library bundle whose feed.bin UNITS records (type 3, full tables and diffs; layout in
server/src/units/srv_units_records.h: u64 loopIter, u32 caller, u32 tableGen, u32 full, u32 count | nUpsert, then 32-byte
SrvFeed::UnitRecord) carry the given units with the visible flag (UnitRecord::flags bit 3) clear, as the DLL's
CvPlot::updateCenterUnit leaves aircraft after a live rebase, a carrier move or a kill on their plot (extras-units.md
4985.1). scene.json's feed.bin bytes and sha256 are updated and the bundle id gets "-SYNHIDE"; nothing else changes, so the
copy's dumps are still the server's frames of the after-load state: the reference the flag-on replay must equal.

    python -I synhide.py SRC_BUNDLE DST_DIR id[,id...]
Exit 0 written; 1 a unit not found in any UNITS record or feed.bin not parsed to its end; 2 usage.
"""
import hashlib
import json
import os
import shutil
import stat
import struct
import sys
from pathlib import Path

VISIBLE = 1 << 3
UNITS = 3


def _patch_units(pay, ids, hits):
    b = bytearray(pay)
    (n,) = struct.unpack_from('<I', b, 20)   # a full table's count or a diff's nUpsert: the records follow either way
    for i in range(n):
        off = 24 + 32 * i
        flags, uid = b[off + 3], struct.unpack_from('<i', b, off + 4)[0]
        if uid in ids:
            b[off + 3] = flags & ~VISIBLE
            hits[uid] = hits.get(uid, 0) + 1
    return bytes(b)


def _writable_then_retry(func, path, _exc):
    os.chmod(path, stat.S_IREAD | stat.S_IWRITE)
    func(path)


def make(src, dst, ids, log=print):
    src, dst = Path(src), Path(dst)
    if dst.exists():
        shutil.rmtree(dst, onerror=_writable_then_retry)
    # content only (copyfile, no copystat): the library copy's files are read-only (scenelib), and feed.bin and scene.json
    # are rewritten below (issue 4982: PermissionError in test_fix_air_bases_scenes E9b against the read-only copy)
    shutil.copytree(src, dst, copy_function=shutil.copyfile)
    data = (dst / 'feed.bin').read_bytes()
    out = bytearray(data[:16])
    pos, hits, nrec = 16, {}, 0
    while pos + 8 <= len(data):
        typ, n = struct.unpack_from('<II', data, pos)
        pay = data[pos + 8:pos + 8 + n]
        if typ == UNITS:
            nrec += 1
            pay = _patch_units(pay, ids, hits)
        out += struct.pack('<II', typ, n) + pay
        pos += 8 + n
    if pos != len(data):
        log(f'synhide: feed.bin not parsed to its end ({pos} of {len(data)} bytes)')
        return 1
    (dst / 'feed.bin').write_bytes(bytes(out))
    sj = json.loads((dst / 'scene.json').read_text())
    sj['members']['feed.bin']['bytes'] = len(out)
    sj['members']['feed.bin']['sha256'] = hashlib.sha256(bytes(out)).hexdigest()
    sj['bundle_id'] = sj['bundle_id'] + '-SYNHIDE'
    (dst / 'scene.json').write_text(json.dumps(sj, indent=1))
    log(f'synhide: {dst.name}: {nrec} UNITS records; hidden (records touched): {dict(sorted(hits.items()))}')
    missing = set(ids) - set(hits)
    if missing:
        log(f'synhide: not found: {sorted(missing)}')
        return 1
    return 0


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    return make(sys.argv[1], sys.argv[2], {int(x) for x in sys.argv[3].split(',')})


if __name__ == '__main__':
    sys.exit(main())
