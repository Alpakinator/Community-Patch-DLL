"""Whole-file comparison of two .Civ5Save files under decision D24 part 1 (writer identity).

  python tools/d10/savecmp.py A.Civ5Save B.Civ5Save [--d18-side A|B|neither] [--json OUT]

Compared: the header (raw bytes, every field), the pregame (raw), the body (inflated) and any bytes after the end of
the compressed stream (raw). Nothing is masked. Exactly two exceptions exist, each applied only when its condition
holds and each reported by name with its byte ranges:

  (a) dll-version-string: the DLL version string, when the two files carry different ones. It is the u32-length
      string at inflated-body offset 0x14 (CURRENT_GAMECORE_VERSION, e.g. "Release-5.4.8-18-g29f15ac38") and the
      header's dllVersion field. The strings are cut out of both files; everything else must then be equal.
  (b) d18-container-order: the order of the records inside the D18 containers (CvCity::m_miInstantYieldsTotal,
      CvExplorationPlotSet::m_aValues, CvGame::m_AdvisorMessagesViewed), only when exactly one of the two writers has
      D18 (--d18-side names that file). The containers are found from the save's structure, not from the
      differences (anchors(), below): each one has a name and a body location before any byte is compared. Every
      differing run of the body must lie inside the records of one anchored container whose count is equal in both
      files, whose records are the same multiset in both files (whole records permuted), whose records in the D18
      file are in strictly ascending key order with keys in the member's range, and which differs in order.
      --d18-side neither: both writers lack D18 (a round trip of a pre-D18 build through another DLL): the same rule
      without the ascending-order condition (the order is the hash table's, which follows insertion history).

The anchors (derived from the DLL's serialization code, fork x64 29f15ac38 and i0-ship e6f904ad6, whose lines below
are identical):
  - CvGame::m_AdvisorMessagesViewed: a walk of CvGame::Serialize (CvGame.cpp:10835-10887 at 29f15ac38): after the
    version string come 33 int fields, 6 bools, one PlayerTypes, 5 bools (147 bytes), then the set
    [u32 count][count x u32]; m_eHandicap and m_ePausePlayer follow and are range-checked.
  - CvExplorationPlotSet::m_aValues, three per player: CvPlayer::Serialize writes CvPlayerPolicies then CvEconomicAI
    (CvPlayer.cpp:45839-45840). CvPlayerPolicies ends with m_aiPolicyModifiers, a vector<int> of
    NUM_POLICY_MODIFIER_TYPE = 60 entries (CvPolicyClasses.h:1028, CvPolicyClasses.cpp:4177); CvEconomicAI::Serialize
    (CvEconomicAI.cpp:345-369) writes N bools m_pabUsingStrategy, N ints m_paiTurnStrategyAdopted (-1 or a turn),
    m_vPlotsToExploreLand, -Coast, -Ocean (each [u32 count][count x (int plot, 3 x int)]), 10 ints and the vector
    m_RequestedSavings (12-byte records); CvCitySpecializationAI follows with 4 bools. N (the number of economic
    strategies, 37 in VP 5.4.8) is not known to the tool: it is the one value for which exactly MAX_PLAYERS = 64 such
    blocks parse, in body order = player 0..63. No other value of N and no other count of blocks is accepted.
  - CvCity::m_miInstantYieldsTotal, one per city: CvCity::Serialize (CvCity.cpp:32424-32431) writes m_aiEconomicValue
    (vector<int> of MAX_CIV_PLAYERS = 63), the maps m_miUnitClassTrainingAllowed and m_miWLTKDFromProject (8-byte
    records), m_miInstantYieldsTotal ([u32 count][count x (int yield, int)]), then four vector<int> of
    NUM_YIELD_TYPES entries (m_aiBaseYieldRateFromReligion ... m_aiYieldFromMinors), then
    m_afResourceQuantityPerXFranchises (R fractions of 8 bytes), m_aiYieldChangeFromCorporationFranchises (Y ints) and
    m_aiResourceQuantityFromPOP (R ints). Every city block found must have the same yield count Y and resource count
    R; the key is 0..Y-1; the number of city blocks must equal CvGame::m_iNumCities (the 10th int of
    CvGame::Serialize). Cities are numbered in body order.
A differing run outside the records of these containers is never attributed to exception (b).

Anything else is a difference, reported with offsets (file offsets for the header and pregame, inflated-body offsets
for the body). Verdicts: "identical" (the files are byte-identical), "identical content" (header, pregame, inflated
body and trailing bytes identical, only the zlib stream encodes them differently; the first differing file offset is
reported), "identical under exceptions", "different". Exit 0 unless "different".

The container reader (split, runs, align_insertion) is taken from tools/d10/evidence/2402/xhost.py (this repository,
team-2400 worker A, 2026-11-10, uncommitted at the time of copying). Standard-library Python 3.11. Written 2026-11-10
by team-2400 worker B (issue 2403); structural anchors, trailing bytes and the 'neither' mode by one-off 9011
(2026-11-10), replacing a search for any ascending-key stretch within 200 KB before a difference.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

VERSION_OFF = 0x14  # inflated body: u32 length + CURRENT_GAMECORE_VERSION (verdiff.py, issue 1008)

# --- structure constants of the DLL (see the module docstring for the source lines) --------------------------------
GAME_BYTES_BEFORE_ADVISOR = 33 * 4 + 6 + 4 + 5  # CvGame::Serialize, version string end -> m_AdvisorMessagesViewed
MAX_PLAYERS = 64
MAX_CIV_PLAYERS = 63
NUM_POLICY_MODIFIER_TYPE = 60
MAX_STRATEGIES = 255
EXPLORE_SETS = ('m_vPlotsToExploreLand', 'm_vPlotsToExploreCoast', 'm_vPlotsToExploreOcean')
MAX_PLOT = 1 << 17  # plot indices: a huge map has 128 x 80 = 10,240 plots

U32 = struct.Struct('<I')
I32 = struct.Struct('<i')


# ---------------------------------------------------------------------------------------------------------------------
# container (after xhost.py)

class R:
    def __init__(self, d: bytes, p: int = 0):
        self.d, self.p = d, p

    def u32(self) -> int:
        v = struct.unpack_from('<I', self.d, self.p)[0]
        self.p += 4
        return v

    def u8(self) -> int:
        v = self.d[self.p]
        self.p += 1
        return v

    def s(self) -> str:
        n = self.u32()
        v = self.d[self.p:self.p + n].decode('latin-1')
        self.p += n
        return v

    def guid(self) -> str:
        v = self.d[self.p:self.p + 16].hex()
        self.p += 16
        return v


def header(d: bytes) -> tuple[dict, dict, int]:
    """saves.md 2.1; returns (fields, field byte ranges [start, end), header length)."""
    if d[:4] != b'CIV5':
        raise ValueError('not a .Civ5Save')
    r = R(d, 4)
    h, rng = {}, {}

    def f(name, fn):
        s = r.p
        h[name] = fn()
        rng[name] = (s, r.p)

    f('saveVersion', r.u32)
    f('gameVersion', r.s)
    f('build', r.s)
    f('turn', r.u32)
    f('gameType', r.u8)
    for k in ('civilization', 'handicap', 'startEra', 'currentEra', 'speed', 'worldSize', 'mapScript'):
        f(k, r.s)
    f('dlc', lambda: [(r.guid(), r.u32(), r.s()) for _ in range(r.u32())])
    f('mods', lambda: [(r.s(), r.u32(), r.s()) for _ in range(r.u32())])
    for k in ('leaderName', 'civName', 'playerColor'):
        f(k, r.s)
    f('dllGuid', r.guid)
    f('dllVersion', r.s)
    f('constGuid', r.guid)
    f('const0', r.u32)
    return h, rng, r.p


def split(path: Path) -> dict:
    """Header, pregame, inflated body and 'trailing': the bytes of the last chunk after the end of the zlib stream."""
    d = Path(path).read_bytes()
    h, rng, hl = header(d)
    n = len(d)
    for p in range(hl, n - 10):
        if d[p:p + 4] == b'\x02\x00\x00\x00' and d[p + 8:p + 10] == b'\x78\x9c':
            q, chunks, ok = p + 4, [], True
            while q < n:
                if q + 4 > n:
                    ok = False
                    break
                ln = struct.unpack_from('<I', d, q)[0]
                if ln == 0 or q + 4 + ln > n:
                    ok = False
                    break
                chunks.append(d[q + 4:q + 4 + ln])
                q += 4 + ln
            if ok and q == n:
                # The game ends the stream with a sync flush (00 00 ff ff), not a final block, so zlib reports no end
                # of stream for real saves [L, 2026-11-10, c5h and server saves]. Bytes after a final block are kept
                # as 'trailing' and compared; bytes after a sync flush are inflated as body data or raise zlib.error.
                z = zlib.decompressobj()
                body = z.decompress(b''.join(chunks))
                return {'raw': d, 'header': d[:hl], 'pregame': d[hl:p], 'pregameOff': hl, 'body': body,
                        'streamEnd': 'final block' if z.eof else 'sync flush (no final block)',
                        'trailing': z.unused_data, 'trailingOff': n - len(z.unused_data),
                        'fields': h, 'ranges': rng, 'sha256': hashlib.sha256(d).hexdigest()}
    raise ValueError(f'{path}: compressed body not found')


def runs(a: bytes, b: bytes, off_a: int = 0, off_b: int = 0, limit: int = 200) -> list[dict]:
    out, i, m = [], 0, min(len(a), len(b))
    while i < m and len(out) < limit:
        if a[i] != b[i]:
            j = i
            while j < m and (a[j] != b[j] or (j + 1 < m and a[j + 1] != b[j + 1])):  # merge runs 1 byte apart
                j += 1
            out.append({'a': off_a + i, 'b': off_b + i, 'len': j - i, 'A': a[i:j].hex()[:128], 'B': b[i:j].hex()[:128]})
            i = j
        else:
            i += 1
    if len(a) != len(b) and len(out) < limit:
        out.append({'a': off_a + m, 'b': off_b + m, 'len': abs(len(a) - len(b)), 'A': a[m:].hex()[:128],
                    'B': b[m:].hex()[:128]})
    return out


def all_runs(a: bytes, b: bytes) -> list[tuple[int, int]]:
    """Every differing run [lo, hi) of two equal-length buffers (runs 1 byte apart merged); no limit."""
    out, i, m = [], 0, len(a)
    while i < m:
        # fast skip of equal stretches
        if a[i:i + 4096] == b[i:i + 4096]:
            i += 4096
            continue
        if a[i] != b[i]:
            j = i
            while j < m and (a[j] != b[j] or (j + 1 < m and a[j + 1] != b[j + 1])):
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def align_insertion(a: bytes, b: bytes) -> dict | None:
    """One insertion of |len(a)-len(b)| bytes (after xhost.py)."""
    d = len(b) - len(a)
    if d == 0:
        return None
    lo, hi = (a, b) if d > 0 else (b, a)
    k = abs(d)
    s = 0
    while s < len(lo) and lo[len(lo) - 1 - s] == hi[len(hi) - 1 - s]:
        s += 1
    cut = len(lo) - s
    i = 0
    while i < cut and lo[i] == hi[i]:
        i += 1
    for p in range(i, cut + 1):
        w = min(4096, cut - p)
        if lo[p:p + w] == hi[p + k:p + k + w]:
            cut = p
            break
    return {'longer': 'B' if d > 0 else 'A', 'bytes': k, 'at': cut, 'inserted': hi[cut:cut + k].hex()[:400]}


# ---------------------------------------------------------------------------------------------------------------------
# exception (a)

def body_version(body: bytes) -> tuple[str, int, int]:
    """(string, start, end) of the DLL version string field (u32 length included) in an inflated body."""
    n = struct.unpack_from('<I', body, VERSION_OFF)[0]
    if n > 256:
        raise ValueError(f'no version string at body offset {VERSION_OFF:#x} (length {n})')
    return body[VERSION_OFF + 4:VERSION_OFF + 4 + n].decode('latin-1'), VERSION_OFF, VERSION_OFF + 4 + n


# ---------------------------------------------------------------------------------------------------------------------
# exception (b): structural anchors of the D18 containers

def _bool_run(S: bytes, s: int, cap: int) -> int:
    """Number of consecutive bytes in {0, 1} from s (at most cap)."""
    n = 0
    end = min(len(S), s + cap)
    while s + n < end and S[s + n] <= 1:
        n += 1
    return n


def _econ_block(S: bytes, s: int, N: int) -> dict | None:
    """Parse one CvEconomicAI block at s with N strategies; None unless every field is in range."""
    if s + 5 * N + 12 > len(S):
        return None
    ints = struct.unpack_from(f'<{N}i', S, s + N)
    if -1 not in ints or not all(-1 <= v < 100000 for v in ints):  # Init sets -1; no player adopts every strategy
        return None
    q = s + 5 * N
    sets = []
    for nm in EXPLORE_SETS:
        if q + 4 > len(S):
            return None
        c = U32.unpack_from(S, q)[0]
        if c > MAX_PLOT or q + 4 + 16 * c > len(S):
            return None
        sets.append((nm, q, c))
        q += 4 + 16 * c
    if q + 44 + 4 > len(S):
        return None
    recon = struct.unpack_from('<2i', S, q)
    if not all(-1 <= v <= 8 for v in recon):  # ReconState enums
        return None
    r = U32.unpack_from(S, q + 40)[0]
    if r > 256 or q + 44 + 12 * r + 4 > len(S):
        return None
    # m_RequestedSavings: CvPurchaseRequest {PurchaseType m_eType (small enum), int m_iAmount, int m_iPriority}
    if any(not -1 <= I32.unpack_from(S, q + 44 + 12 * k)[0] < 32 for k in range(r)):
        return None
    q2 = q + 44 + 12 * r
    if any(x > 1 for x in S[q2:q2 + 4]):  # CvCitySpecializationAI: 4 bools
        return None
    return {'start': s, 'sets': sets, 'end': q2}


def _anchor_economic_ai(S: bytes) -> tuple[list[dict], str]:
    key = U32.pack(NUM_POLICY_MODIFIER_TYPE)
    byN: dict[int, list[dict]] = {}
    pos = S.find(key)
    while pos >= 0:
        s = pos + 4 + 4 * NUM_POLICY_MODIFIER_TYPE
        for N in range(1, _bool_run(S, s, MAX_STRATEGIES) + 1):
            blk = _econ_block(S, s, N)
            if blk:
                byN.setdefault(N, []).append(blk)
        pos = S.find(key, pos + 1)
    fits = [N for N, bl in byN.items() if len(bl) == MAX_PLAYERS]
    if len(fits) != 1:
        return [], (f'CvEconomicAI anchor not found: no unique strategy count N with exactly {MAX_PLAYERS} blocks '
                    f'(blocks per N: {dict(sorted((k, len(v)) for k, v in byN.items() if len(v) > 4))})')
    N = fits[0]
    out = []
    for player, blk in enumerate(byN[N]):
        for nm, q, c in blk['sets']:
            out.append({'name': f'player {player} CvEconomicAI::{nm} (CvExplorationPlotSet::m_aValues)',
                        'member': 'CvExplorationPlotSet::m_aValues', 'countAt': q, 'count': c, 'recordBytes': 16,
                        'keyType': 'int', 'keyMax': MAX_PLOT})
    return out, f'CvEconomicAI: {MAX_PLAYERS} player blocks, N = {N} strategies'


def _anchor_cities(S: bytes, ver_end: int) -> tuple[list[dict], str]:
    num_cities = I32.unpack_from(S, ver_end + 9 * 4)[0]  # CvGame::m_iNumCities, the 10th int of CvGame::Serialize
    key = U32.pack(MAX_CIV_PLAYERS)
    hits = []
    pos = S.find(key)
    while pos >= 0:
        q = pos + 4 + 4 * MAX_CIV_PLAYERS
        maps, ok = [], True
        for _ in range(3):
            if q + 4 > len(S):
                ok = False
                break
            c = U32.unpack_from(S, q)[0]
            if c > 4096:
                ok = False
                break
            maps.append((q, c))
            q += 4 + 8 * c
        if ok:
            ys, r = [], q
            for _ in range(4):
                if r + 4 > len(S):
                    ok = False
                    break
                y = U32.unpack_from(S, r)[0]
                if not 1 <= y <= 64:
                    ok = False
                    break
                ys.append(y)
                r += 4 + 4 * y
            if ok and len(set(ys)) == 1:
                # then m_afResourceQuantityPerXFranchises (vector<fraction>, R resources, 8 B each),
                # m_aiYieldChangeFromCorporationFranchises (Y ints), m_aiResourceQuantityFromPOP (R ints)
                Y = ys[0]
                if r + 4 <= len(S):
                    R_ = U32.unpack_from(S, r)[0]
                    r2 = r + 4 + 8 * R_
                    if R_ <= 4096 and r2 + 4 <= len(S) and U32.unpack_from(S, r2)[0] == Y:
                        r3 = r2 + 4 + 4 * Y
                        if r3 + 4 <= len(S) and U32.unpack_from(S, r3)[0] == R_:
                            hits.append((pos, maps[2], (Y, R_), r3 + 4 + 4 * R_))
        pos = S.find(key, pos + 1)
    if not hits:
        return [], 'CvCity: no city block found'
    # every real city block has the same (yield, resource) counts and there are m_iNumCities of them; a chance match
    # elsewhere has other counts (seen on the huge save: (2, 2) and (5, 5)) and is dropped
    per = Counter(h[2] for h in hits)
    fit = [yr for yr, n in per.items() if n == num_cities]
    if len(fit) != 1:
        return [], (f'CvCity anchor rejected: no unique (yield, resource) count pair with CvGame::m_iNumCities = '
                    f'{num_cities} blocks ({dict(per)})')
    Y, NR = fit[0]
    hits = [h for h in hits if h[2] == (Y, NR)]
    if any(hits[i][3] > hits[i + 1][0] for i in range(len(hits) - 1)):
        return [], 'CvCity anchor rejected: overlapping city blocks'
    out = [{'name': f'city block {k} CvCity::m_miInstantYieldsTotal', 'member': 'CvCity::m_miInstantYieldsTotal',
            'countAt': q, 'count': c, 'recordBytes': 8, 'keyType': 'int', 'keyMax': Y}
           for k, (pos, (q, c), y, r) in enumerate(hits)]
    return out, f'CvCity: {len(hits)} city blocks, {Y} yield types, {NR} resources'


def _anchor_game(S: bytes, ver_end: int) -> tuple[list[dict], str]:
    q = ver_end + GAME_BYTES_BEFORE_ADVISOR
    if q + 4 > len(S):
        return [], 'CvGame: body too short'
    c = U32.unpack_from(S, q)[0]
    e = q + 4 + 4 * c
    if c > 100000 or e + 8 > len(S):
        return [], f'CvGame anchor rejected: m_AdvisorMessagesViewed count {c}'
    handicap, pause = struct.unpack_from('<2i', S, e)
    if not (0 <= handicap < 32 and -1 <= pause < MAX_PLAYERS):
        return [], f'CvGame anchor rejected: m_eHandicap {handicap}, m_ePausePlayer {pause} after the walk'
    return ([{'name': 'CvGame::m_AdvisorMessagesViewed', 'member': 'CvGame::m_AdvisorMessagesViewed', 'countAt': q,
              'count': c, 'recordBytes': 4, 'keyType': 'uint', 'keyMax': 1 << 32}],
            f'CvGame: m_AdvisorMessagesViewed at body {q} (count {c})')


def anchors(S: bytes, ver_end: int) -> dict:
    """The D18 containers of one inflated body (ver_end = end of the version string field in S's coordinates)."""
    cs, notes = [], []
    for fn in (lambda: _anchor_game(S, ver_end), lambda: _anchor_economic_ai(S), lambda: _anchor_cities(S, ver_end)):
        c, n = fn()
        cs += c
        notes.append(n)
    cs.sort(key=lambda x: x['countAt'])
    return {'containers': cs, 'notes': notes}


def _check_container(A: bytes, B: bytes, ct: dict, d18: str) -> str | None:
    """None when the container holds the same records in both files and the D18 side (if any) is sorted, else why."""
    q, c, rec = ct['countAt'], ct['count'], ct['recordBytes']
    if U32.unpack_from(A, q)[0] != c or U32.unpack_from(B, q)[0] != c:
        return 'count differs'
    s = q + 4
    ra = [A[s + k * rec:s + (k + 1) * rec] for k in range(c)]
    rb = [B[s + k * rec:s + (k + 1) * rec] for k in range(c)]
    if Counter(ra) != Counter(rb):
        return 'records differ (not a permutation)'
    fmt = '<I' if ct['keyType'] == 'uint' else '<i'
    for side, recs in (('A', ra), ('B', rb)):
        keys = [struct.unpack_from(fmt, r)[0] for r in recs]
        if not all(0 <= k < ct['keyMax'] for k in keys):
            return f'key out of range on {side}'
        if len(set(keys)) != len(keys):
            return f'duplicate key on {side}'
        if d18 == side and any(keys[i] >= keys[i + 1] for i in range(len(keys) - 1)):
            return f'D18 side {side} not in ascending key order'
    return None


# ---------------------------------------------------------------------------------------------------------------------
# compare

def compare(pa: Path, pb: Path, d18_side: str | None = None) -> dict:
    """d18_side: 'A' or 'B' when exactly that file's writer has D18; 'neither' when both writers lack it (exception
    (b) without the order condition); None: exception (b) does not apply."""
    a, b = split(pa), split(pb)
    res = {'a': str(pa), 'b': str(pb), 'aSha256': a['sha256'], 'bSha256': b['sha256'],
           'bytes': [len(a['raw']), len(b['raw'])], 'd18Side': d18_side,
           'exceptions': [], 'differences': []}
    if a['raw'] == b['raw']:
        res['verdict'] = 'identical'
        return res
    exc, diffs = res['exceptions'], res['differences']

    # exception (a): the version strings, when they differ
    va, sa, ea = body_version(a['body'])
    vb, sb, eb = body_version(b['body'])
    ha, hb = a['fields']['dllVersion'], b['fields']['dllVersion']
    hra, hrb = a['ranges']['dllVersion'], b['ranges']['dllVersion']
    cut_header = ha != hb
    cut_body = va != vb
    if cut_body or cut_header:
        e = {'name': 'dll-version-string', 'exception': '(a)', 'ranges': []}
        if cut_body:
            e['ranges'].append({'part': 'body (inflated)', 'a': [sa, ea], 'b': [sb, eb], 'aValue': va, 'bValue': vb})
        if cut_header:
            e['ranges'].append({'part': 'header (dllVersion)', 'a': list(hra), 'b': list(hrb), 'aValue': ha, 'bValue': hb})
        exc.append(e)

    # header
    HA, HB = a['header'], b['header']
    if cut_header:
        HA = HA[:hra[0]] + HA[hra[1]:]
        HB = HB[:hrb[0]] + HB[hrb[1]:]
    if HA != HB:
        fields = {k: [a['fields'][k], b['fields'][k]] for k in a['fields']
                  if a['fields'][k] != b['fields'][k] and k != 'dllVersion'}
        d = {'part': 'header', 'bytes': [len(a['header']), len(b['header'])], 'fields': {}}
        for k, (x, y) in fields.items():
            d['fields'][k] = {'a': x, 'b': y, 'aRange': list(a['ranges'][k]), 'bRange': list(b['ranges'][k])}
        if not fields:
            d['runs'] = runs(HA, HB)
        diffs.append(d)

    # pregame (raw, nothing excepted)
    if a['pregame'] != b['pregame']:
        diffs.append({'part': 'pregame', 'fileOffset': [a['pregameOff'], b['pregameOff']],
                      'bytes': [len(a['pregame']), len(b['pregame'])],
                      'runs': runs(a['pregame'], b['pregame'], a['pregameOff'], b['pregameOff'])})

    # bytes after the end of the compressed stream (raw, nothing excepted)
    if a['trailing'] != b['trailing']:
        diffs.append({'part': 'trailing bytes after the compressed stream',
                      'fileOffset': [a['trailingOff'], b['trailingOff']],
                      'bytes': [len(a['trailing']), len(b['trailing'])],
                      'runs': runs(a['trailing'], b['trailing'], a['trailingOff'], b['trailingOff'])})
    elif a['trailing']:
        res['trailingNote'] = f"both files carry the same {len(a['trailing'])} bytes after the compressed stream"

    # body
    A, B = a['body'], b['body']
    sha = shb = 0
    if cut_body:
        A, B = A[:sa] + A[ea:], B[:sb] + B[eb:]
        sha, shb = ea - sa, eb - sb
    ver_end = sa if cut_body else ea  # end of the version string field in the coordinates of A and B

    def ra_(off):  # excised offset -> real body offset in A / B
        return off + sha if cut_body and off >= sa else off

    def rb_(off):
        return off + shb if cut_body and off >= sb else off

    if A != B:
        if len(A) != len(B):
            ins = align_insertion(A, B)
            d = {'part': 'body', 'bytes': [len(a['body']), len(b['body'])],
                 'note': 'lengths differ (after the exceptions): no permutation of records can explain it',
                 'insertion': ins}
            c, k = ins['at'], ins['bytes']
            d['insertion']['atA'] = ra_(c)
            pre = runs(A[:c], B[:c])
            post = runs(A[c:], B[c + k:], c, c + k) if ins['longer'] == 'B' else runs(A[c + k:], B[c:], c + k, c)
            d['runs'] = [dict(r, a=ra_(r['a']), b=rb_(r['b'])) for r in pre + post]
            diffs.append(d)
        else:
            rr = all_runs(A, B)
            unattributed, containers, rejected = [], [], []
            anc = None
            if d18_side in ('A', 'B', 'neither'):
                aa, ab = anchors(A, ver_end), anchors(B, ver_end)
                sig = lambda x: [(c['name'], c['countAt']) for c in x['containers']]  # noqa: E731
                if sig(aa) != sig(ab):
                    res['anchorNote'] = ('exception (b) unavailable: the two files do not have the same D18 '
                                         f'containers (A: {aa["notes"]}; B: {ab["notes"]})')
                else:
                    anc = aa
                    res['anchors'] = {'notes': aa['notes'], 'containers': len(aa['containers'])}
            cl = anc['containers'] if anc else []
            starts = [c['countAt'] + 4 for c in cl]
            used: dict[int, dict] = {}
            for lo, hi in rr:
                k = bisect.bisect_right(starts, lo) - 1
                ct = cl[k] if k >= 0 else None
                if ct is None or not (ct['countAt'] + 4 <= lo and hi <= ct['countAt'] + 4 + ct['count'] *
                                      ct['recordBytes']):
                    unattributed.append((lo, hi))
                    continue
                h = used.get(k)
                if h is None:
                    why = _check_container(A, B, ct, d18_side)
                    h = used[k] = dict(ct, why=why, runs=[])
                h['runs'].append((lo, hi))
            for k, h in sorted(used.items()):
                if h['why'] is not None:
                    unattributed += h['runs']
                    rejected.append(f"{h['name']} at body A {ra_(h['countAt'])}: {h['why']}")
                    continue
                e = h['countAt'] + 4 + h['count'] * h['recordBytes']
                containers.append({'name': h['name'], 'member': h['member'], 'countAt': h['countAt'],
                                   'count': h['count'], 'recordBytes': h['recordBytes'], 'keyType': h['keyType'],
                                   'aRange': [ra_(h['countAt']), ra_(e)], 'bRange': [rb_(h['countAt']), rb_(e)],
                                   'differingRuns': len(h['runs']),
                                   'differingBytes': sum(y - x for x, y in h['runs'])})
            unattributed.sort()
            if containers:
                byname = Counter(h['member'] for h in containers)
                exc.append({'name': 'd18-container-order', 'exception': '(b)', 'd18Side': d18_side,
                            'containers': len(containers), 'byContainer': dict(byname),
                            'bytes': sum(h['differingBytes'] for h in containers), 'ranges': containers})
            if unattributed:
                d = {'part': 'body', 'bytes': [len(a['body']), len(b['body'])],
                     'differingRuns': len(unattributed),
                     'differingBytes': sum(h - l for l, h in unattributed),
                     'runs': [{'a': ra_(l), 'b': rb_(l), 'len': h - l, 'A': A[l:h].hex()[:128],
                               'B': B[l:h].hex()[:128]} for l, h in unattributed[:200]]}
                if rejected:
                    d['rejectedContainers'] = rejected
                if res.get('anchorNote'):
                    d['note'] = res['anchorNote']
                diffs.append(d)
    if diffs:
        res['verdict'] = 'different'
    elif exc:
        res['verdict'] = 'identical under exceptions'
    else:
        # header, pregame, trailing bytes and inflated body equal: only the zlib stream differs
        ra, rb = a['raw'], b['raw']
        i = next((k for k in range(min(len(ra), len(rb))) if ra[k] != rb[k]), min(len(ra), len(rb)))
        res['verdict'] = 'identical content'
        res['note'] = (f'header, pregame and inflated body identical; the compressed body stream differs from file '
                       f'offset {i} (file sizes {len(ra)} / {len(rb)} B)')
    return res


def text(res: dict) -> str:
    L = [f"A {res['a']}\n  sha256 {res['aSha256']} ({res['bytes'][0]} B)",
         f"B {res['b']}\n  sha256 {res['bSha256']} ({res['bytes'][1]} B)",
         f"verdict: {res['verdict'].upper()}"]
    if res.get('note'):
        L.append(f"note: {res['note']}")
    if res.get('trailingNote'):
        L.append(f"note: {res['trailingNote']}")
    if res.get('anchors'):
        L.append(f"D18 containers anchored: {res['anchors']['containers']} ({'; '.join(res['anchors']['notes'])})")
    if res.get('anchorNote'):
        L.append(res['anchorNote'])
    for e in res['exceptions']:
        if e['name'] == 'dll-version-string':
            L.append(f"exception (a) dll-version-string:")
            for r in e['ranges']:
                L.append(f"  {r['part']}: A [{r['a'][0]}, {r['a'][1]}) {r['aValue']!r}; B [{r['b'][0]}, {r['b'][1]}) "
                         f"{r['bValue']!r}")
        else:
            side = ('neither writer has D18: order not checked' if e['d18Side'] == 'neither'
                    else f"D18 writer = {e['d18Side']}")
            L.append(f"exception (b) d18-container-order ({side}): {e['containers']} containers, "
                     f"{e['bytes']} differing bytes, {e['byContainer']}")
            for h in e['ranges'][:60]:
                order = ('' if e['d18Side'] == 'neither'
                         else f", D18 side sorted by {h['keyType']} key")
                L.append(f"  {h['name']}: body A [{h['aRange'][0]}, {h['aRange'][1]}) B [{h['bRange'][0]}, "
                         f"{h['bRange'][1]}): count {h['count']} x {h['recordBytes']} B, same records{order}; "
                         f"{h['differingRuns']} runs, {h['differingBytes']} B differ")
            if len(e['ranges']) > 60:
                L.append(f"  ... {len(e['ranges']) - 60} more (JSON report)")
    for d in res['differences']:
        L.append(f"DIFFERENCE in {d['part']} ({d['bytes'][0]} / {d['bytes'][1]} B)")
        for k, v in d.get('fields', {}).items():
            if k == 'dlc':
                sx, sy = set(map(tuple, v['a'])), set(map(tuple, v['b']))
                L.append(f"  header field dlc A {v['aRange']} B {v['bRange']}: {len(v['a'])} vs {len(v['b'])} entries; "
                         f"only in A {sorted(x[2] for x in sx - sy)}; only in B {sorted(x[2] for x in sy - sx)}")
            else:
                L.append(f"  header field {k} A {v['aRange']} B {v['bRange']}: {v['a']!r} vs {v['b']!r}")
        if 'note' in d:
            L.append(f"  {d['note']}")
        for x in d.get('rejectedContainers', []):
            L.append(f"  anchored container not excepted: {x}")
        if d.get('insertion'):
            i = d['insertion']
            L.append(f"  insertion of {i['bytes']} B in {i['longer']} at A@{i['atA']}: {i['inserted'][:96]}")
        if 'differingRuns' in d:
            L.append(f"  {d['differingRuns']} differing runs not explained by an exception, {d['differingBytes']} B")
        for r in d.get('runs', [])[:40]:
            L.append(f"  run A@{r['a']} B@{r['b']} len {r['len']}: {r['A'][:64]} / {r['B'][:64]}")
        if len(d.get('runs', [])) > 40:
            L.append(f"  ... {len(d['runs']) - 40} more runs (JSON report)")
    return '\n'.join(L) + '\n'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--d18-side', choices=['A', 'B', 'neither'],
                    help='A or B: the file whose writer has D18 when the other lacks it; neither: both lack it')
    ap.add_argument('--anchors', action='store_true', help='print the anchored D18 containers of A and exit')
    ap.add_argument('--json')
    a = ap.parse_args(argv)
    if a.anchors:
        s = split(Path(a.a))
        anc = anchors(s['body'], body_version(s['body'])[2])
        for n in anc['notes']:
            print(n)
        for c in anc['containers']:
            if c['count']:
                print(f"  {c['name']}: count at body {c['countAt']}, {c['count']} x {c['recordBytes']} B")
        return 0
    res = compare(Path(a.a), Path(a.b), a.d18_side)
    if a.json:
        Path(a.json).write_text(json.dumps(res, indent=1), encoding='utf-8')
    print(text(res), end='')
    return 1 if res['verdict'] == 'different' else 0


if __name__ == '__main__':
    sys.exit(main())
