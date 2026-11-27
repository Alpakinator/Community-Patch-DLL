"""Ticket 0727: checks that the EXE Minimap system's behaviour the proxy relies on (proxy/src/proxy_minimap_fog.h) is the
same on build 26577625 as on build 4390913, from the two golden copies' bytes. Read only; prints counts, names and
verdicts, never bytes or another build's addresses (D83, D202).

    python -I proxy/tests/minimap_fog/layout_check.py --old <4390913 EXE> --new <26577625 EXE>

For each function of research/05-graphics-composition/observer-switch-minimap.md 6.2 and 6.6 (the fog handler, the
cell of (column, row), the block of a slot, the active-player handler, the view step, the render entry), at its
build-4390913 address: carried to the new build by tools/exebuild/addr_match.py (match_code), then every byte of the
whole function compared with the old one wherever neither image's mask covers it (a relocated dword or a branch's
rel32). Equal bytes mean the same structure offsets, constants and bit tests. Each relocated dword of the old function
that names one of the four .data items of proxy.tsv (mmf_slot, mmf_blocks, mmf_system, mmf_wrap) must name the same
item in the new function (its carried address, from addr_match.match_data). The handler's neighbour-direction table
(its one relocated .data operand outside those four) must hold the same 18 values in both builds.
The six directions of proxy/src/proxy_minimap_fog.h (kDirections) must be the old build's table.
--break (the negative run): the handler is compared byte for byte like the others and one direction value is changed
before the compares; the run must then FAIL (exit 1) on exactly three checks: the handler's bytes, kDirections and the
direction table.
Exit 0 when every check passes, 1 otherwise, 2 bad arguments.
"""
import argparse
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(REPO, 'tools', 'exebuild'))
import addr_match as am  # noqa: E402

# (name, VA in 4390913, size in bytes): the decompile's function sizes (Ghidra, 2026-11-24, ticket 0552)
# The fog handler of build 26577625 is compiled differently (registers and order from its +0x3D on; 838 bytes, not 839):
# its behaviour and offsets were re-read by a decompile of that build (observer-switch-minimap.md 6.6, ticket 0727,
# 2026-11-25) [C]. Here it is checked by the minimap items its relocated operands name (the same set as the old one's).
HANDLER_VA = 0x6A5400
FUNCS = [
    ('fog handler (HexFOWStateChanged)', HANDLER_VA, 839),
    ('cell of (column, row)', 0x6E0A10, 102),
    ('block of a slot', 0x4A0AA0, 691),
    ('active-player handler', 0x7162E0, 91),
    ('view step (box to world)', 0x48B3F0, 1412),
    ('render entry', 0x72C310, 32),
]
DATA = [('mmf_slot', 0x1A0C338), ('mmf_blocks', 0x1A0C340), ('mmf_system', 0x1A0C670), ('mmf_wrap', 0x1A0C568)]
DIRS_VA = 0xC8FB08  # the handler's neighbour-direction table: 6 x (dx, dy, dz) ints (observer-switch-minimap.md 6.6)
DIRS_REF = 0xC8FB10  # the address the handler's code names (the third int of the first row)


def ints(img, va, n):
    pe = img.pe
    s = pe.section_of(va - img.base)
    off = va - img.base - s['rva'] + s['raw_off']
    return list(struct.unpack_from('<%di' % n, pe.data, off))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', required=True)
    ap.add_argument('--new', required=True)
    ap.add_argument('--break', dest='brk', action='store_true', help='the negative run (see the module text)')
    a = ap.parse_args()
    if not (os.path.isfile(a.old) and os.path.isfile(a.new)):
        print('layout_check: --old and --new must be the two EXE files')
        return 2
    old, new = am.Img(a.old), am.Img(a.new)
    bad = 0
    new_data = {}
    for name, va in DATA:
        nv, method, votes, conf = am.match_data(old, new, va)
        ok = nv is not None and conf == 'high'
        new_data[va] = nv
        print('%s %s carried (%s, %s, %s)' % ('ok  ' if ok else 'FAIL', name, method, votes, conf))
        bad += not ok
    nd, method, votes, conf = am.match_data(old, new, DIRS_REF)
    new_dirs_ref = nd if conf == 'high' else None
    print('%s the direction table carried (%s, %s, %s)' % ('ok  ' if new_dirs_ref else 'FAIL', method, votes, conf))
    bad += new_dirs_ref is None
    for name, va, size in FUNCS:
        nv, method, votes, conf = am.match_code(old, new, va)
        if nv is None or conf not in ('high', 'medium'):
            print('FAIL %s: not carried (%s, %s)' % (name, method, conf))
            bad += 1
            continue
        o_lo = va - old.base - old.t_rva
        n_lo = nv - new.base - new.t_rva
        if va == HANDLER_VA and not a.brk:
            items = dict((new_data[v], n) for n, v in DATA)
            if new_dirs_ref:
                items[new_dirs_ref] = 'the direction table'
            want = set(n for n, v in DATA if any((old.t_rva + o_lo + k) in old.relset and
                                                 struct.unpack_from('<I', old.text, o_lo + k)[0] == v
                                                 for k in range(size - 3))) | {'the direction table'}
            got = set(items[struct.unpack_from('<I', new.text, n_lo + k)[0]] for k in range(size - 3)
                      if (new.t_rva + n_lo + k) in new.relset and struct.unpack_from('<I', new.text, n_lo + k)[0] in items)
            ok = want == got
            print('%s %s: carried (%s, %s); compiled differently (decompile, 6.6); names %s %s'
                  % ('ok  ' if ok else 'FAIL', name, method, conf, ', '.join(sorted(got)),
                     'as the old one' if ok else 'BUT the old one names ' + ', '.join(sorted(want))))
            bad += not ok
            continue
        same = all(old.mask[o_lo + k] or new.mask[n_lo + k] or old.text[o_lo + k] == new.text[n_lo + k]
                   for k in range(size))
        masks = all(bool(old.mask[o_lo + k]) == bool(new.mask[n_lo + k]) for k in range(size))
        refs = 0
        refs_bad = 0
        for k in range(size - 3):
            if (old.t_rva + o_lo + k) not in old.relset:
                continue
            ot = struct.unpack_from('<I', old.text, o_lo + k)[0]
            nt = struct.unpack_from('<I', new.text, n_lo + k)[0]
            want = new_data.get(ot) if ot in new_data else (new_dirs_ref if ot == DIRS_REF else None)
            if ot in new_data or ot == DIRS_REF:
                refs += 1
                refs_bad += nt != want
        ok = same and masks and refs_bad == 0
        print('%s %s: carried (%s, %s); %d bytes %s; masks %s; %d references to the minimap items, %d differ'
              % ('ok  ' if ok else 'FAIL', name, method, conf, size, 'equal outside the masks' if same else 'DIFFER',
                 'aligned' if masks else 'NOT aligned', refs, refs_bad))
        bad += not ok
    hdr = open(os.path.join(REPO, 'proxy', 'src', 'proxy_minimap_fog.h'), encoding='utf-8').read()
    m = re.search(r'kDirections\[6\]\[3\] = \{(.*?)\};', hdr)
    code = [int(v) for v in re.findall(r'-?\d+', m.group(1))] if m else []
    od = ints(old, DIRS_VA, 18)
    if a.brk:
        od[0] += 1
    ok = code == od
    print('%s proxy_minimap_fog.h kDirections: %d values, %s' % ('ok  ' if ok else 'FAIL', len(code),
                                                                 "the old build's table" if ok else "NOT the old build's table"))
    bad += not ok
    if new_dirs_ref:
        ndv = ints(new, new_dirs_ref - (DIRS_REF - DIRS_VA), 18)
        ok = od == ndv
        print('%s the direction table: %d values %s' % ('ok  ' if ok else 'FAIL', len(od), 'equal' if ok else 'DIFFER'))
        bad += not ok
    print('layout_check: %s (%d failed)' % ('PASS' if not bad else 'FAIL', bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
