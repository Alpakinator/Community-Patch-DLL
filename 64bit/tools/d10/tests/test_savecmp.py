"""Negative and positive tests of tools/d10/savecmp.py (issue 9011).

  python -m unittest discover -s tools/d10/tests -v

Every case of the verification's negtests.py (tools/d10/evidence/verification-2400/negtests.py, cases S01-S21 and the
S3x reorderings outside the D18 containers) is here, built from one real resave pair in data/:
  A = tiny-x86T25-resave-x86-i0ship.Civ5Save  (x86 i0-ship e6f904ad6, no D18; the x86 turn-25 save of the tiny game)
  B = tiny-x86T25-resave-x64-29f15ac3.Civ5Save (x64 29f15ac38, D18; the same input)
made by `run_d10.py --games tiny` on 2026-11-10 (o9011 tiny-main run). B is the D18 writer. Offline, about a minute.
On the tool of main e2e7ab7..88485af the S21 and S3x cases fail (they were accepted there).

The two saves are game output and are not exported (ticket 0815): without them the SaveCmp cases are skipped with a
reason, and the SyntheticSave cases below, which build small saves of the same container format in code, still run.
"""
from __future__ import annotations

import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import savecmp  # noqa: E402

A = HERE / 'data' / 'tiny-x86T25-resave-x86-i0ship.Civ5Save'
B = HERE / 'data' / 'tiny-x86T25-resave-x64-29f15ac3.Civ5Save'


def write(path: Path, s: dict, body: bytes, header=None, pregame=None, tail=b'', finish=False) -> Path:
    """A save from parts; finish=False ends the stream with a sync flush like the game, True with a final block."""
    if finish:
        z = zlib.compress(body)
    else:
        c = zlib.compressobj()
        z = c.compress(body) + c.flush(zlib.Z_SYNC_FLUSH)
    z += tail
    path.write_bytes((header if header is not None else s['header']) +
                     (pregame if pregame is not None else s['pregame']) + struct.pack('<II', 2, len(z)) + z)
    return path


HAVE_SAVES = A.is_file() and B.is_file()


def synthetic(path: Path, version: str = 'Release-test-1', body_tail: bytes = b'body' * 64, pregame: bytes = b'PRE',
              turn: int = 25, tail: bytes = b'', split_at: int = 0, finish: bool = False) -> Path:
    """A minimal save of savecmp's container format, no game data: the header (savecmp.header's fields), a pregame,
    then [u32 2][u32 len][zlib stream] with the DLL version string at inflated-body offset 0x14. split_at > 0 adds a
    sync flush after that many body bytes (another stream, the same content); finish ends with a final block, after
    which tail is kept as trailing bytes."""
    def s(t: str) -> bytes:
        return struct.pack('<I', len(t)) + t.encode('latin-1')
    h = b'CIV5' + struct.pack('<I', 8) + s('1.0.3.279') + s('403694') + struct.pack('<I', turn) + bytes([0])
    h += b''.join(s(x) for x in ('CIVILIZATION_X', 'HANDICAP_X', 'ERA_X', 'ERA_X', 'GAMESPEED_X', 'WORLDSIZE_X',
                                 'Assets/Maps/X.lua'))
    h += struct.pack('<I', 0) + struct.pack('<I', 0)  # no DLC, no mods
    h += s('LEADER_X') + s('CIV_X') + s('PLAYERCOLOR_X')
    h += bytes(range(16)) + s(version) + bytes(range(16, 32)) + struct.pack('<I', 0)
    body = bytes(0x14) + s(version) + body_tail
    c = zlib.compressobj()
    z = c.compress(body[:split_at]) + c.flush(zlib.Z_SYNC_FLUSH) if split_at else b''
    z += c.compress(body[split_at:]) + c.flush(zlib.Z_FINISH if finish else zlib.Z_SYNC_FLUSH) + tail
    path.write_bytes(h + pregame + struct.pack('<II', 2, len(z)) + z)
    return path


class SyntheticSave(unittest.TestCase):
    """The verdicts on saves built in code (runs without the test saves, e.g. in the export)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='d10-savecmp-syn-')
        self.W = Path(self.tmp.name)
        self.a = synthetic(self.W / 'a.Civ5Save')

    def tearDown(self):
        self.tmp.cleanup()

    def verdict(self, b: Path, side=None) -> str:
        return savecmp.compare(self.a, b, side)['verdict']

    def test_split_reads_the_parts(self):
        sp = savecmp.split(self.a)
        self.assertEqual(sp['fields']['turn'], 25)
        self.assertEqual(sp['fields']['dllVersion'], 'Release-test-1')
        self.assertEqual(sp['pregame'], b'PRE')
        self.assertEqual(savecmp.body_version(sp['body'])[0], 'Release-test-1')
        self.assertEqual(sp['streamEnd'], 'sync flush (no final block)')

    def test_identical(self):
        self.assertEqual(self.verdict(synthetic(self.W / 'b.Civ5Save')), 'identical')
        self.assertEqual(savecmp.main([str(self.a), str(self.a)]), 0)

    def test_identical_content_other_compression(self):
        self.assertEqual(self.verdict(synthetic(self.W / 'b.Civ5Save', split_at=40)), 'identical content')

    def test_version_string_exception(self):
        r = savecmp.compare(self.a, synthetic(self.W / 'b.Civ5Save', version='Release-test-22'))
        self.assertEqual(r['verdict'], 'identical under exceptions')
        self.assertEqual([e['name'] for e in r['exceptions']], ['dll-version-string'])

    def test_differences(self):
        for name, kw in (('body', {'body_tail': b'body' * 63 + b'bodY'}), ('turn', {'turn': 26}),
                         ('pregame', {'pregame': b'PRF'}), ('length', {'body_tail': b'body' * 65}),
                         ('trailing', {'tail': b'GARBAGE!', 'finish': True})):
            with self.subTest(name):
                b = synthetic(self.W / f'b-{name}.Civ5Save', **kw)
                self.assertEqual(self.verdict(b, 'B'), 'different')
                self.assertEqual(savecmp.main([str(self.a), str(b)]), 1)

    def test_not_a_save(self):
        p = self.W / 'x.Civ5Save'
        p.write_bytes(b'NOPE' + bytes(64))
        with self.assertRaises(ValueError):
            savecmp.split(p)


@unittest.skipUnless(HAVE_SAVES, 'the two test saves of tests/data/ are not present (they are not exported)')
class SaveCmp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='d10-savecmp-')
        cls.W = Path(cls.tmp.name)
        cls.sa, cls.sb = savecmp.split(A), savecmp.split(B)
        cls.bb, cls.ab = cls.sb['body'], cls.sa['body']
        cls.base = savecmp.compare(A, B, 'B')
        cls.Bw = write(cls.W / 'B-rewritten.Civ5Save', cls.sb, cls.bb)
        cls.Aw = write(cls.W / 'A-rewritten.Civ5Save', cls.sa, cls.ab)
        v, cls.vs, cls.ve = savecmp.body_version(cls.bb)
        # anchored containers of B (own coordinates) and of A (A's version string is one byte longer)
        cls.used = [r for e in cls.base['exceptions'] if e['name'] == 'd18-container-order' for r in e['ranges']]
        if hasattr(savecmp, 'anchors'):
            cls.anc_b = savecmp.anchors(cls.bb, cls.ve)['containers']
            cls.anc_a = savecmp.anchors(cls.ab, savecmp.body_version(cls.ab)[2])['containers']
        else:  # main's savecmp (no anchors): the containers its baseline comparison used, as the verification did
            cls.anc_b = [{'countAt': r['bRange'][0], 'count': r['count'], 'recordBytes': r['recordBytes']}
                         for r in cls.used]
            cls.anc_a = [{'countAt': r['aRange'][0], 'count': r['count'], 'recordBytes': r['recordBytes']}
                         for r in cls.used]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def cmp(self, a, b, side='B'):
        return savecmp.compare(Path(a), Path(b), side)

    def ok(self, a, b, side='B'):
        r = self.cmp(a, b, side)
        self.assertNotEqual(r['verdict'], 'different', savecmp.text(r)[:2000])
        return r

    def bad(self, a, b, side='B'):
        r = self.cmp(a, b, side)
        self.assertEqual(r['verdict'], 'different', savecmp.text(r)[:2000])
        return r

    def free_b(self, off: int, margin: int = 8) -> bool:
        """Outside every anchored container of B (records and count) and the version string."""
        if self.vs - margin <= off < self.ve + margin:
            return False
        return all(not (c['countAt'] - margin <= off < c['countAt'] + 4 + c['count'] * c['recordBytes'] + margin)
                   for c in self.anc_b)

    # ---------------------------------------------------------------- baseline and anchors
    def test_S01_baseline(self):
        self.assertEqual(self.base['verdict'], 'identical under exceptions')
        self.ok(A, self.Bw)
        names = {e['name'] for e in self.base['exceptions']}
        self.assertEqual(names, {'dll-version-string', 'd18-container-order'})

    def test_anchors_found_and_named_by_location(self):
        notes = savecmp.anchors(self.bb, self.ve)['notes']
        self.assertIn('64 player blocks, N = 37 strategies', notes[1])
        self.assertIn('12 city blocks', notes[2])
        self.assertTrue(self.used)
        for r in self.used:
            self.assertRegex(r['name'], r'^player \d+ CvEconomicAI::m_vPlotsToExplore(Land|Coast|Ocean) ')

    # ---------------------------------------------------------------- outside the exceptions
    def test_S02_body_bytes_outside(self):
        for frac in (0.1, 0.5, 0.9):
            off = int(len(self.bb) * frac)
            while not self.free_b(off):
                off += 64
            x = bytearray(self.bb)
            x[off] ^= 1
            self.bad(A, write(self.W / f'S02-{off}.Civ5Save', self.sb, bytes(x)))

    def test_S03_header_turn(self):
        h = bytearray(self.sb['header'])
        h[self.sb['ranges']['turn'][0]] ^= 1
        self.bad(A, write(self.W / 'S03.Civ5Save', self.sb, self.bb, header=bytes(h)))

    def test_S04_pregame(self):
        pg = bytearray(self.sb['pregame'])
        pg[len(pg) // 2] ^= 1
        self.bad(A, write(self.W / 'S04.Civ5Save', self.sb, self.bb, pregame=bytes(pg)))

    def test_S05_S06_version_neighbours(self):
        for i, off in enumerate((self.ve, self.vs - 1)):
            x = bytearray(self.bb)
            x[off] ^= 1
            self.bad(A, write(self.W / f'S05-{i}.Civ5Save', self.sb, bytes(x)))

    def test_S07_version_text_same_length_allowed(self):
        x = bytearray(self.bb)
        x[self.vs + 4] = ord('Z')
        self.ok(A, write(self.W / 'S07.Civ5Save', self.sb, bytes(x)))

    def test_S08_version_length_swallows_next_byte(self):
        x = bytearray(self.bb)
        struct.pack_into('<I', x, self.vs, (self.ve - self.vs - 4) + 1)
        self.bad(A, write(self.W / 'S08.Civ5Save', self.sb, bytes(x)))

    def test_S09_S10_same_version_strings(self):
        va = savecmp.body_version(self.ab)[0].encode()
        x = self.bb[:self.vs] + struct.pack('<I', len(va)) + va + self.bb[self.ve:]
        self.ok(A, write(self.W / 'S09.Civ5Save', self.sb, x))
        y = bytearray(x)
        y[self.vs + 4 + len(va) + 3] ^= 1
        self.bad(A, write(self.W / 'S10.Civ5Save', self.sb, bytes(y)))

    def test_S11_S12_header_dllversion(self):
        hr = self.sb['ranges']['dllVersion']
        h = bytearray(self.sb['header'])
        h[hr[1] - 1] ^= 1
        self.ok(A, write(self.W / 'S11.Civ5Save', self.sb, self.bb, header=bytes(h)))
        h = bytearray(self.sb['header'])
        h[hr[1]] ^= 1
        self.bad(A, write(self.W / 'S12.Civ5Save', self.sb, self.bb, header=bytes(h)))

    # ---------------------------------------------------------------- inside a real D18 container
    def container(self):
        c0 = self.used[len(self.used) // 2]
        return c0, c0['bRange'][0] + 4, c0['recordBytes'], len(self.ab) - len(self.bb)

    def test_S13_D18_side_value_changed(self):
        c0, s0, rec, sh = self.container()
        x = bytearray(self.bb)
        x[s0 + rec - 1] ^= 1
        self.bad(A, write(self.W / 'S13.Civ5Save', self.sb, bytes(x)))

    def test_S14_S15_non_D18_side_value_and_key_changed(self):
        c0, s0, rec, sh = self.container()
        xa = bytearray(self.ab)
        xa[s0 + sh + rec - 1] ^= 1
        self.bad(write(self.W / 'S14A.Civ5Save', self.sa, bytes(xa)), self.Bw)
        xa = bytearray(self.ab)
        k = struct.unpack_from('<i', xa, s0 + sh)[0]
        struct.pack_into('<i', xa, s0 + sh, k + 100000)
        self.bad(write(self.W / 'S15A.Civ5Save', self.sa, bytes(xa)), self.Bw)

    def test_S16_D18_side_unsorted(self):
        c0, s0, rec, sh = self.container()
        x = bytearray(self.bb)
        r0, r1 = bytes(x[s0:s0 + rec]), bytes(x[s0 + rec:s0 + 2 * rec])
        x[s0:s0 + rec], x[s0 + rec:s0 + 2 * rec] = r1, r0
        self.bad(A, write(self.W / 'S16.Civ5Save', self.sb, bytes(x)))

    def test_S17_D18_side_count_changed(self):
        c0, s0, rec, sh = self.container()
        x = bytearray(self.bb)
        struct.pack_into('<I', x, c0['bRange'][0], c0['count'] - 1)
        self.bad(A, write(self.W / 'S17.Civ5Save', self.sb, bytes(x)))

    def test_S18_S19_wrong_or_no_side(self):
        self.bad(A, B, 'A')
        self.bad(A, B, None)

    def test_S20_insertion(self):
        x = self.bb[:len(self.bb) // 2] + b'\0\0\0\0' + self.bb[len(self.bb) // 2:]
        self.bad(A, write(self.W / 'S20.Civ5Save', self.sb, x))

    # ---------------------------------------------------------------- holes of the verification
    def test_S21_trailing_bytes_after_a_final_block(self):
        """Fails on main: the bytes after the end of the stream were ignored."""
        r = self.bad(A, write(self.W / 'S21.Civ5Save', self.sb, self.bb, tail=b'GARBAGE!', finish=True))
        self.assertIn('trailing bytes after the compressed stream', [d['part'] for d in r['differences']])

    def test_S21b_trailing_bytes_after_a_sync_flush(self):
        p = write(self.W / 'S21b.Civ5Save', self.sb, self.bb, tail=b'GARBAGE!')
        try:
            r = self.cmp(A, p)
        except Exception:  # zlib.error: run_d10 reports 'compare failed', a failure
            return
        self.assertEqual(r['verdict'], 'different')

    def probe(self, rec, fmt, key_ok, want_n=6):
        """The verification's S3x: reverse an ascending [count][c x rec] stretch outside every D18 container, in A
        (the non-D18 writer) only. Must fail."""
        sh = len(self.ab) - len(self.bb)
        found, pos, step = 0, 0x200, max(1, len(self.bb) // 4000)
        while pos < len(self.bb) - 64 and found < want_n:
            c = struct.unpack_from('<I', self.bb, pos)[0]
            if 2 <= c <= 64 and pos + 4 + c * rec < len(self.bb) and self.free_b(pos) and \
                    self.free_b(pos + 4 + c * rec):
                keys = [struct.unpack_from(fmt, self.bb, pos + 4 + i * rec)[0] for i in range(c)]
                recs = [self.bb[pos + 4 + i * rec:pos + 4 + (i + 1) * rec] for i in range(c)]
                if all(key_ok(k) for k in keys) and all(keys[i] < keys[i + 1] for i in range(c - 1)) \
                        and len(set(recs)) == c and \
                        self.ab[pos + sh:pos + sh + 4 + c * rec] == self.bb[pos:pos + 4 + c * rec]:
                    xa = bytearray(self.ab)
                    p0 = pos + sh + 4
                    xa[p0:p0 + c * rec] = b''.join(reversed(recs))
                    found += 1
                    with self.subTest(rec=rec, pos=pos, count=c):
                        self.bad(write(self.W / f'P-{rec}-{pos}.Civ5Save', self.sa, bytes(xa)), self.Bw)
                    pos += 4 + c * rec + step * 50
                    continue
            pos += 4 if rec == 4 else 1
        self.assertEqual(found, want_n)

    def test_S3x_reorder_outside_u32x4(self):
        self.probe(4, '<I', lambda k: True)

    def test_S3x_reorder_outside_i32x8(self):
        self.probe(8, '<i', lambda k: 0 <= k < 64)

    def test_S3x_reorder_outside_i32x16(self):
        self.probe(16, '<i', lambda k: k >= 0)

    def test_reorder_of_an_anchored_container_on_the_non_D18_side_is_allowed(self):
        """The rule itself: the non-D18 writer may write any order inside an anchored container."""
        sh = len(self.ab) - len(self.bb)
        c = next(c for c in self.anc_b if c['recordBytes'] == 16 and c['count'] >= 3)
        s = c['countAt'] + 4 + sh
        recs = [self.ab[s + 16 * i:s + 16 * (i + 1)] for i in range(c['count'])]
        xa = bytearray(self.ab)
        xa[s:s + 16 * c['count']] = b''.join(reversed(recs))
        r = self.ok(write(self.W / 'R-allowed.Civ5Save', self.sa, bytes(xa)), self.Bw)
        self.assertTrue(any(x['name'] == 'd18-container-order' for x in r['exceptions']))

    def test_instant_yields_container_reordered_on_non_D18_side_allowed_and_value_change_fails(self):
        sh = len(self.ab) - len(self.bb)
        c = next((c for c in self.anc_b if c['recordBytes'] == 8 and c['count'] >= 2), None)
        if c is None:
            self.skipTest('no city with two instant-yield entries in this save')
        s = c['countAt'] + 4 + sh
        recs = [self.ab[s + 8 * i:s + 8 * (i + 1)] for i in range(c['count'])]
        xa = bytearray(self.ab)
        xa[s:s + 8 * c['count']] = b''.join(reversed(recs))
        self.ok(write(self.W / 'IY-ok.Civ5Save', self.sa, bytes(xa)), self.Bw)
        xa[s + 7] ^= 1
        self.bad(write(self.W / 'IY-bad.Civ5Save', self.sa, bytes(xa)), self.Bw)

    # ---------------------------------------------------------------- --d18-side neither (MC, 2026-11-10)
    def neither_variant(self, name, fn):
        x = bytearray(self.ab)
        fn(x)
        return write(self.W / f'N-{name}.Civ5Save', self.sa, bytes(x))

    def test_neither_mode(self):
        c = next(c for c in self.anc_a if c['recordBytes'] == 16 and c['count'] >= 3)
        s, n = c['countAt'] + 4, c['count']
        recs = [self.ab[s + 16 * i:s + 16 * (i + 1)] for i in range(n)]

        def permute(x):
            x[s:s + 16 * n] = b''.join(reversed(recs))
        p = self.neither_variant('perm', permute)
        self.ok(self.Aw, p, 'neither')            # same multiset, other order, no side sorted: allowed
        self.bad(self.Aw, p, None)                # but only in that explicit mode

        def perm_and_change(x):
            permute(x)
            x[s + 5] ^= 1
        self.bad(self.Aw, self.neither_variant('change', perm_and_change), 'neither')

        def count(x):
            permute(x)
            struct.pack_into('<I', x, s - 4, n - 1)
        self.bad(self.Aw, self.neither_variant('count', count), 'neither')
        # a reordering outside the containers fails in this mode too
        sh = len(self.ab) - len(self.bb)
        off = next(o for o in range(0x2000, len(self.bb), 997) if self.free_b(o, 64)
                   and self.ab[o + sh:o + sh + 8] != self.ab[o + sh + 8:o + sh + 16])

        def outside(x):
            permute(x)
            a, b_ = bytes(x[off + sh:off + sh + 8]), bytes(x[off + sh + 8:off + sh + 16])
            x[off + sh:off + sh + 8], x[off + sh + 8:off + sh + 16] = b_, a
        self.bad(self.Aw, self.neither_variant('outside', outside), 'neither')


if __name__ == '__main__':
    unittest.main()
