"""addr_match.py - carry EXE addresses from one build to another by bytes, not by position (ticket 0582, 2026-11-19).

Origin: a copy of research/02-exe-internals/evidence/0582/addr_match.py at commit 94a825474 (ticket 0582), taken into
tools/exebuild/ on 2026-11-19 by ticket 4960 (epic 0590) for gen_table.py; the evidence copy stays as it was. Changes
after the copy are in this repository's history.

    python -I addr_match.py OLD.exe NEW.exe VAS.txt|VAS.csv [--out map.csv] [--truth truth.csv]

VAS: one VA per line (hex, VA at the old ImageBase), or inventory.py's CSV (column "va"). For every address:
  code (.text): the bytes from the address forward, with every relocated dword (old .reloc) and every rel32 of a
      call/jmp/jcc masked, are searched in the new .text; windows of 16..160 bytes, the first unique hit wins
      ("bytes"). If none is unique, each direct call site (E8 rel32 -> the address) is matched by its context and the
      new call's target is read ("callers", majority vote).
  data (.data/.rdata/.bss/other): every instruction in old .text whose relocated dword equals the address is matched
      by its masked context (24 bytes either side) in new .text and the new dword at the same spot is read ("xrefs",
      majority vote). Addresses inside a structure (no exact reference) use the nearest referenced address below
      within 0x400 and add the offset ("xrefs+off", lower confidence).
Output columns: old_va, old_section, new_va, new_section, method, votes, confidence (high: unique bytes or >=2
agreeing xrefs with no dissent; medium: one xref or a split vote won 2:1; low: +off or callers with one vote).
--truth: CSV old_va,new_va of known pairs; prints how many the matcher got right, wrong and missing.
Reads only. Prints addresses and counts, never bytes of the EXE (D83).
"""
import collections
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe32  # noqa: E402


class Img:
    def __init__(self, path):
        self.pe = pe32.PE(path)
        self.base = self.pe.image_base
        t = next(s for s in self.pe.sections if s["name"] == ".text")
        self.t_rva = t["rva"]
        self.text = self.pe.data[t["raw_off"]:t["raw_off"] + min(t["raw_size"], max(t["vsize"], 1))]
        self.t_end = self.t_rva + len(self.text)
        self.relset = set(self.pe.relocs)
        # mask[i] True = byte i of .text is position-dependent (reloc dword or rel32 of a branch)
        m = bytearray(len(self.text))
        for r in self.pe.relocs:
            if self.t_rva <= r < self.t_end - 3:
                o = r - self.t_rva
                m[o:o + 4] = b"\1\1\1\1"
        tx = self.text
        n = len(tx)
        i = tx.find(b"\xE8")
        while 0 <= i < n - 5:
            m[i + 1:i + 5] = b"\1\1\1\1"
            i = tx.find(b"\xE8", i + 1)
        i = tx.find(b"\xE9")
        while 0 <= i < n - 5:
            m[i + 1:i + 5] = b"\1\1\1\1"
            i = tx.find(b"\xE9", i + 1)
        i = tx.find(b"\x0F")
        while 0 <= i < n - 6:
            if 0x80 <= tx[i + 1] <= 0x8F:
                m[i + 2:i + 6] = b"\1\1\1\1"
            i = tx.find(b"\x0F", i + 1)
        self.mask = bytes(m)
        self._xrefs = None
        self._calls = None

    def sec(self, va):
        s = self.pe.section_of(va - self.base)
        return s["name"] if s else "?"

    def xrefs(self):
        """value -> [offset in .text of the relocated dword]"""
        if self._xrefs is None:
            d = collections.defaultdict(list)
            for r in self.pe.relocs:
                if self.t_rva <= r < self.t_end - 3:
                    o = r - self.t_rva
                    d[struct.unpack_from("<I", self.text, o)[0]].append(o)
            self._xrefs = d
        return self._xrefs

    def calls(self):
        """target VA -> [offset of the E8 byte]"""
        if self._calls is None:
            d = collections.defaultdict(list)
            tx = self.text
            i = tx.find(b"\xE8")
            n = len(tx)
            while 0 <= i < n - 5:
                tgt = self.base + self.t_rva + i + 5 + struct.unpack_from("<i", tx, i + 1)[0]
                if self.base + self.t_rva <= tgt < self.base + self.t_end:
                    d[tgt].append(i)
                i = tx.find(b"\xE8", i + 1)
            self._calls = d
        return self._calls


def pattern(img, off, lo, hi, extra_mask=()):
    """Runs of fixed bytes in img.text[off+lo, off+hi) as [(rel, bytes)], rel to off."""
    a, b = max(0, off + lo), min(len(img.text), off + hi)
    runs, cur, start = [], bytearray(), None
    for k in range(a, b):
        fixed = not img.mask[k] and (k - off) not in extra_mask
        if fixed:
            if start is None:
                start = k
            cur.append(img.text[k])
        elif start is not None:
            runs.append((start - off, bytes(cur)))
            cur, start = bytearray(), None
    if start is not None:
        runs.append((start - off, bytes(cur)))
    return runs


def search(new, runs, limit=3):
    """Offsets o in new.text where every run matches at o+rel. Up to `limit` hits."""
    if not runs:
        return []
    anchor = max(runs, key=lambda r: len(r[1]))
    if len(anchor[1]) < 4:
        return []
    hits = []
    tx = new.text
    i = tx.find(anchor[1])
    while i >= 0:
        o = i - anchor[0]
        ok = o >= 0
        if ok:
            for rel, bs in runs:
                p = o + rel
                if tx[p:p + len(bs)] != bs:
                    ok = False
                    break
        if ok:
            hits.append(o)
            if len(hits) >= limit:
                break
        i = tx.find(anchor[1], i + 1)
    return hits


def fixed_len(runs):
    return sum(len(b) for _, b in runs)


def body_ambiguous(old, va, w=16):
    """True when the masked bytes from va on (the first w) also stand somewhere else in the OLD .text (ticket 4960c,
    2026-11-20): two look-alike pieces of code, for example two 0x20-byte wrappers that differ only in their call's rel32
    (uid_delete_array / uid_free_aligned, swapped by the byte windows on build 26577625 because a longer window ran into
    their neighbours, whose order the relink changed). Such an address is not carried by its own bytes."""
    off = va - old.base - old.t_rva
    runs = pattern(old, off, 0, w)
    return fixed_len(runs) >= 10 and len(search(old, runs, 2)) > 1


def match_code(old, new, va):
    off = va - old.base - old.t_rva
    if body_ambiguous(old, va):
        # only something outside the look-alike bytes can tell the copies apart: the address's own callers, or the
        # function it lies in (carried by that function's callers) with every masked byte up to it equal
        r = _match_code_callers(old, new, va)
        if r[0] is not None:
            return r[0], r[1] + " (body ambiguous)", r[2], r[3]
        r = match_in_function(old, new, va)
        if r[0] is not None:
            return r
        return None, "ambiguous", 0, "ambiguous"
    for w in (16, 24, 32, 48, 64, 96, 128, 160):
        runs = pattern(old, off, 0, w)
        if fixed_len(runs) < 10:
            continue
        h = search(new, runs, 2)
        if len(h) == 1:
            return new.base + new.t_rva + h[0], "bytes%d" % w, 1, "high"
        if not h:
            break  # longer windows cannot match either
    for w in (16, 32, 48, 64, 96, 128):  # windows ending a little after the address (mid-function addresses)
        runs = pattern(old, off, -w, 8)
        if fixed_len(runs) < 10:
            continue
        h = search(new, runs, 2)
        if len(h) == 1:
            return new.base + new.t_rva + h[0], "back%d" % w, 1, "high"
        if not h:
            break
    return _match_code_callers(old, new, va)


def masked_equal(old, new, o_lo, o_hi, n_lo, pad=0):
    """old.text[o_lo, o_hi) and new.text[n_lo, ...) equal wherever neither image's mask covers a byte; at least 8 bytes
    compared. Offsets into .text."""
    o_lo, o_hi, n_lo = o_lo - pad, o_hi + pad, n_lo - pad
    if o_lo < 0 or n_lo < 0 or o_hi > len(old.text) or n_lo + (o_hi - o_lo) > len(new.text):
        return False
    fixed = 0
    for k in range(o_hi - o_lo):
        if old.mask[o_lo + k] or new.mask[n_lo + k]:
            continue
        if old.text[o_lo + k] != new.text[n_lo + k]:
            return False
        fixed += 1
    return fixed >= 8


def match_in_function(old, new, va, reach=0x4000):
    """A mid-function address whose own bytes have a look-alike (ticket 4960c, 2026-11-20): the nearest direct-call
    target F at or below va (the start of the function it lies in, as far as direct calls show), carried by F's callers
    alone (not by bytes, which the look-alike may share), and every masked byte from F to 16 bytes past va equal at F'.
    Then va' = F' + (va - F). A call target that is not the enclosing function's start (a virtual-only function before
    it) gives a span that crosses into other code; the relink reordered functions, so such a span does not match and
    nothing is taken."""
    calls = old.calls()
    starts = [f for f in calls if va - reach <= f <= va]
    for f in sorted(starts, reverse=True)[:3]:
        r = _match_code_callers(old, new, f)
        # a single caller whose 64-byte context is unique in the new .text is as strong as a 64-byte window: taken
        # here (medium) because the span check below must hold as well
        single = r[0] is not None and r[2] == "1/1"
        if r[0] is None or (r[3] not in ("high", "medium") and not single):
            continue
        fo, no = f - old.base - old.t_rva, r[0] - new.base - new.t_rva
        if masked_equal(old, new, fo, va - old.base - old.t_rva + 16, no):
            return r[0] + (va - f), "in-function 0x%X+0x%X (callers %s)" % (f, va - f, r[2]), r[2], \
                ("medium" if single else r[3])
    # the function's start by its padding (MSVC places functions on 16-byte boundaries after int3 fill), carried by its
    # own bytes when those are unique in the old .text (an ordinary carry), then the same span check
    f = function_start_by_padding(old, va, reach)
    if f is not None and not body_ambiguous(old, f):
        r = _match_code_bytes(old, new, f)
        if r[0] is not None:
            fo, no = f - old.base - old.t_rva, r[0] - new.base - new.t_rva
            if masked_equal(old, new, fo, va - old.base - old.t_rva + 16, no):
                return r[0] + (va - f), "in-function 0x%X+0x%X (%s)" % (f, va - f, r[1]), r[2], r[3]
    # the whole function prefix [start, va + 16) as one window, never crossing the function's start: unique in the old
    # .text (so the look-alike is in another function whose prefix differs) and unique in the new
    if f is not None and va - f <= 0x1000:
        fo = f - old.base - old.t_rva
        runs = pattern(old, fo, 0, va - f + 16)
        if fixed_len(runs) >= 16 and len(search(old, runs, 2)) == 1:
            h = search(new, runs, 2)
            if len(h) == 1:
                return new.base + new.t_rva + h[0] + (va - f), "in-function 0x%X+0x%X (prefix window)" % (f, va - f), \
                    1, "high"
        r = _match_by_callees(old, new, f, va, runs)
        if r[0] is not None:
            return r
    # the function's start through its caller: a call site whose own address carries (by any route of match_code, two
    # levels at most) and whose new call goes to F'; then the same span check
    for f0 in sorted(starts, reverse=True)[:3]:
        fo = f0 - old.base - old.t_rva
        for c in old.calls().get(f0, [])[:4]:
            cn = _carry_plain(old, new, old.base + old.t_rva + c)
            if cn is None:
                continue
            fn = _call_target(new, cn - new.base - new.t_rva)
            if fn and masked_equal(old, new, fo, va - old.base - old.t_rva + 16, fn - new.base - new.t_rva):
                return fn + (va - f0), "in-function 0x%X+0x%X (its call at 0x%X)" % (f0, va - f0,
                                                                                 old.base + old.t_rva + c), 1, "high"
    return None, "none", 0, ""


def _call_target(img, off):
    """The target VA of an E8 at .text offset off, or None."""
    if off < 0 or off + 5 > len(img.text) or img.text[off] != 0xE8:
        return None
    return img.base + img.t_rva + off + 5 + struct.unpack_from("<i", img.text, off + 1)[0]


_CALLEE_DEPTH = [0]


def _carry_plain(old, new, va):
    """A call target carried by match_code itself, at most two levels deep (a callee that is a look-alike too is
    resolved the same way, but the chain stops there)."""
    if _CALLEE_DEPTH[0] >= 2:
        return None
    _CALLEE_DEPTH[0] += 1
    try:
        r = match_code(old, new, va)
    finally:
        _CALLEE_DEPTH[0] -= 1
    return r[0] if r[0] is not None and r[3] in ("high", "medium") else None


def _match_by_callees(old, new, f, va, runs):
    """Look-alike functions (template copies) whose prefixes are equal byte for byte but whose direct calls go to
    different functions (ticket 4960c: cut_size's three copies differ in the call at +0x3A). The calls inside the prefix
    whose targets differ between the old copies are the evidence: each target is carried on its own; of the new copies
    of the prefix, exactly one must call every carried target at the same offset. Then va' = that copy + (va - f)."""
    fo = f - old.base - old.t_rva
    old_hits = search(old, runs, 8)
    new_hits = search(new, runs, 8)
    if len(old_hits) < 2 or not new_hits or fo not in old_hits:
        return None, "none", 0, ""
    span = va - f + 16
    distinguishing = []
    for k in range(span - 4):
        t = _call_target(old, fo + k)
        if t is None:
            continue
        others = {_call_target(old, h + k) for h in old_hits if h != fo}
        if t not in others:
            distinguishing.append((k, t))
    if not distinguishing:
        return None, "none", 0, ""
    carried = []
    for k, t in distinguishing:
        tn = _carry_plain(old, new, t)
        if tn is not None:
            carried.append((k, tn))
    if not carried:
        return None, "none", 0, ""
    good = [h for h in new_hits if all(_call_target(new, h + k) == tn for k, tn in carried)]
    if len(good) != 1:
        return None, "none", 0, ""
    return new.base + new.t_rva + good[0] + (va - f), "in-function 0x%X+0x%X (callees %s)" % (
        f, va - f, ",".join("+0x%X" % k for k, _ in carried)), "%d/%d" % (len(carried), len(distinguishing)), "high"


def function_start_by_padding(img, va, reach=0x4000):
    """The nearest 16-byte-aligned address at or below va that follows an int3 (0xCC) fill byte, or None."""
    off = va - img.base - img.t_rva
    p = off - (off % 16)
    lo = max(16, off - reach)
    while p >= lo:
        if img.text[p - 1] == 0xCC and img.text[p] != 0xCC:
            return img.base + img.t_rva + p
        p -= 16
    return None


def _match_code_bytes(old, new, va):
    """Forward byte windows only (16..160 B, first unique hit in the new .text): (va, method, votes, conf)."""
    off = va - old.base - old.t_rva
    for w in (16, 24, 32, 48, 64, 96, 128, 160):
        runs = pattern(old, off, 0, w)
        if fixed_len(runs) < 10:
            continue
        h = search(new, runs, 2)
        if len(h) == 1:
            return new.base + new.t_rva + h[0], "bytes%d" % w, 1, "high"
        if not h:
            break
    return None, "none", 0, ""


def _match_code_callers(old, new, va):
    """Each direct caller's context (64 bytes, the call's rel32 masked) found once in the new .text votes for its call
    target."""
    votes = collections.Counter()
    for c in old.calls().get(va, [])[:40]:
        runs = pattern(old, c, -32, 32, extra_mask=(1, 2, 3, 4))
        h = search(new, runs, 2)
        if len(h) == 1 and new.text[h[0]] == 0xE8:
            tgt = new.base + new.t_rva + h[0] + 5 + struct.unpack_from("<i", new.text, h[0] + 1)[0]
            votes[tgt] += 1
    if votes:
        (tgt, n), = votes.most_common(1)
        tot = sum(votes.values())
        conf = "high" if n >= 2 and n == tot else ("medium" if n >= 2 and n * 3 >= tot * 2 else "low")
        return tgt, "callers", "%d/%d" % (n, tot), conf
    return None, "none", 0, ""


def match_vtable(old, new, va, limit=40):
    """A table of code pointers (vtable) in .rdata: map its entries as code, then find the place in the new .rdata
    where most mapped entries line up (added 2026-11-19 for the engine-utility vftable 0x9EA718, 264 entries)."""
    ents = []
    while len(ents) < limit:
        e = old.pe.u32(va - old.base + 4 * len(ents))
        if e is None or not (old.base + old.t_rva <= e < old.base + old.t_end):
            break
        ents.append(e)
    if len(ents) < 2:
        return None, "none", 0, ""
    rd = next(s for s in new.pe.sections if s["name"] == ".rdata")
    raw = new.pe.data[rd["raw_off"]:rd["raw_off"] + rd["raw_size"]]
    votes = collections.Counter()
    n_mapped = 0
    for k, e in enumerate(ents):
        m = match_code(old, new, e)[0]
        if not m:
            continue
        n_mapped += 1
        pat = struct.pack("<I", m)
        p = raw.find(pat)
        while p >= 0:
            if p % 4 == 0 and p >= 4 * k:
                votes[new.base + rd["rva"] + p - 4 * k] += 1
            p = raw.find(pat, p + 1)
    if not votes:
        return None, "none", 0, ""
    (best, n), = votes.most_common(1)
    second = votes.most_common(2)[1][1] if len(votes) > 1 else 0
    conf = "high" if n >= 3 and n >= 3 * second else ("medium" if n >= 2 and n > second else "low")
    return best, "vtable", "%d/%d" % (n, n_mapped), conf


def match_data(old, new, va):
    r = _match_data_xrefs(old, new, va)
    if (r[0] is None or r[3] == "low") and old.sec(va) == ".rdata":
        v = match_vtable(old, new, va)
        if v[0] is not None and (r[0] is None or v[3] in ("high", "medium")):
            return v
    return r


def _match_data_xrefs(old, new, va):
    xr = old.xrefs()
    delta = 0
    sites = xr.get(va, [])
    if not sites:
        cands = [v for v in xr if va - 0x400 <= v < va]
        if cands:
            b = max(cands)
            delta, sites = va - b, xr[b]
    votes = collections.Counter()
    for o in sites[:60]:
        runs = pattern(old, o, -24, 28, extra_mask=(0, 1, 2, 3))
        if fixed_len(runs) < 12:
            continue
        h = search(new, runs, 2)
        if len(h) == 1:
            votes[struct.unpack_from("<I", new.text, h[0])[0] + delta] += 1
    if not votes:
        return None, "none", 0, ""
    (nv, n), = votes.most_common(1)
    tot = sum(votes.values())
    if delta:
        return nv, "xrefs+off", "%d/%d" % (n, tot), "low"
    conf = "high" if n >= 2 and n == tot else ("medium" if n * 3 >= tot * 2 else "low")
    return nv, "xrefs", "%d/%d" % (n, tot), conf


def read_vas(p):
    out = []
    if p.lower().endswith(".csv"):
        with open(p, encoding="utf-8") as f:
            for row in csv.DictReader(f):
                out.append(int(row.get("va") or row.get("old_va"), 16))
    else:
        with open(p, encoding="utf-8") as f:
            for line in f:
                line = line.split("#")[0].strip()
                if line:
                    out.append(int(line.split()[0].split(",")[0], 16))
    seen, res = set(), []
    for v in out:
        if v not in seen:
            seen.add(v)
            res.append(v)
    return res


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    old, new = Img(argv[1]), Img(argv[2])
    vas = read_vas(argv[3])
    rows = []
    for va in vas:
        s = old.sec(va)
        if s == ".text":
            nv, how, votes, conf = match_code(old, new, va)
        else:
            nv, how, votes, conf = match_data(old, new, va)
        rows.append(("0x%X" % va, s, "0x%X" % nv if nv else "", new.sec(nv) if nv else "", how, votes, conf))
    c = collections.Counter((r[1], r[6] or "unmatched") for r in rows)
    print("addresses %d; by old section and confidence: %s" % (len(rows), dict(sorted(c.items()))))
    if "--out" in argv:
        with open(argv[argv.index("--out") + 1], "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["old_va", "old_section", "new_va", "new_section", "method", "votes", "confidence"])
            w.writerows(rows)
    if "--truth" in argv:
        truth = {}
        with open(argv[argv.index("--truth") + 1], encoding="utf-8") as f:
            for row in csv.reader(f):
                if row and not row[0].startswith("#") and row[0] != "old_va":
                    truth[int(row[0], 16)] = int(row[1], 16)
        got = {int(r[0], 16): (int(r[2], 16) if r[2] else None, r[6]) for r in rows}
        right = wrong = miss = 0
        for k, v in truth.items():
            g, conf = got.get(k, (None, ""))
            if g is None:
                miss += 1
            elif g == v:
                right += 1
            else:
                wrong += 1
                print("  WRONG 0x%X -> 0x%X (truth 0x%X, %s)" % (k, g, v, conf))
        print("truth: %d right, %d wrong, %d unmatched of %d" % (right, wrong, miss, len(truth)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
