"""gen_table.py - generate the local address table of one EXE build from the symbol catalogue (epic 0590, ticket 4960).

    python -I tools/exebuild/gen_table.py --old OLD_DX11.exe --new NEW_DX11.exe --steam-build N [--tables DIR]
                                          [--patterns FILE ...] [--allow-bad NAME ...]
    python -I tools/exebuild/gen_table.py --identity --old OLD_DX11.exe [--tables DIR] [--patterns FILE ...]

--old is the DX11 EXE of the anchor build (4390913; checked by its PE stamp and SizeOfImage). Every symbol of the
catalogue (exebuild/symbols/*.tsv) is carried to --new by its kind:
  code     addr_match.match_code (masked bytes forward, then backward windows, then the direct callers' vote)
  data     addr_match.match_data (the instructions that reference it, matched by masked context; vote)
  rdata    as data (with the vtable fallback of addr_match)
  vtable   addr_match.match_vtable (entries carried as code, then lined up in the new .rdata), checked against the
           references (xrefs); the higher confidence wins
  shader   the .shader section's shift (new section VA - old section VA), confirmed by the blob's bytes (the DXBC
           container's whole length, else 32 bytes)
  iat      the import slot of the same DLL!Function in the new EXE, by name (d3dx9_42 = d3dx9_43, d3dx11_42 = d3dx11_43,
           D3DCompiler_42 = D3DCompiler_43)
  array    every address cell of the rows in the named file, per column kind (code, data, rdata, shader; keep = copied)
High and medium confidence are taken; low is not (it goes to review.tsv as a candidate and the symbol stays unresolved).
Then DIR/<N>/hand-review.tsv (name, va, research reference; `name[row][col]` for an array cell) overrides: the answers a
person gave for what the matcher could not carry. Then every pattern template of the files in exebuild/patterns.txt
(and --patterns) is checked on --new with the carried operands (E8/E9 rel32 free, `??` free).

Writes DIR/<N>/table.json (and DIR/<N>/review.tsv: the unresolved and low-confidence symbols with candidates). DIR is
--tables, else the setting exe_tables (tools/release/settings.py; environment variable C5_EXE_TABLES). Stored locally
only (D202): never committed, pushed or quoted. Prints names, counts and OK/BAD only: no EXE bytes and no VA of --new.

--identity writes the anchor build's own table (every value = its anchor, method identity), after checking every anchor
on --old (section, import name, DXBC blob) and every pattern on --old.

Exit: 0 written, complete; 4 written, incomplete (unresolved symbols or array cells: see review.tsv, answer them in
hand-review.tsv and run again); 1 refused, nothing written: a pattern is BAD and not named by --allow-bad; 2 usage, input
or catalogue error. Formats and the hotfix procedure: research/02-exe-internals/exe-build-table.md.
"""
from __future__ import annotations

import argparse
import collections
import datetime
import hashlib
import json
import os
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import table  # noqa: E402
from table import builds  # noqa: E402

IMAGE_BASE = 0x400000
SECTION_OF_KIND = {'code': '.text', 'data': '.data', 'rdata': '.rdata', 'vtable': '.rdata', 'shader': '.shader',
                   'iat': '.rdata'}
TAKEN = ('high', 'medium', 'identity', 'hand')


class GenError(Exception):
    pass


# ------------------------------------------------------------------------------------------------ pattern templates
X_RE = re.compile(r'X\(\s*([A-Za-z0-9_]+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"([^"]*)"\s*\)')
TOK_RE = re.compile(r'\s*(?:R\(([^)]*)\)|C\(([^)]*)\)|(\?\?)|([0-9A-Fa-f]{2})|(\S))')


def read_pattern_tables(files):
    """[(file, name, at, template)] from the X(at, "name", "bytes") tables of the files (repository paths or paths)."""
    out = []
    for f in files:
        p = Path(f)
        if not p.is_absolute():
            p = table.REPO / p
        if not p.is_file():
            raise GenError(f'pattern file {f} not found')
        rel = p.resolve().relative_to(table.REPO).as_posix() if str(p.resolve()).lower().startswith(
            str(table.REPO).lower()) else p.name
        found = X_RE.findall(p.read_text(encoding='utf-8', errors='replace'))
        if not found:
            raise GenError(f'pattern file {f}: no X(addr, "name", "bytes") rows')
        out += [(rel, name, at, tmpl) for at, name, tmpl in found]
    return out


def pattern_files(extra=()):
    files = []
    if table.PATTERNS_LIST.is_file():
        for line in table.PATTERNS_LIST.read_text(encoding='utf-8').splitlines():
            s = line.split('#')[0].strip()
            if s:
                files.append(s)
    for f in extra:
        if f not in files:
            files.append(f)
    return files


def parse_template(tmpl):
    """Tokens of a template: ('b', value) literal byte, ('any',) for ??, ('r', operand text) for R(...), ('c', operand
    text) for C(...): the rel32 of the E8/E9 literal right before it, which must lead to that symbol (ticket 4960c).
    GenError on bad syntax."""
    toks, pos = [], 0
    tmpl = tmpl.rstrip()
    while pos < len(tmpl):
        m = TOK_RE.match(tmpl, pos)
        if not m:
            break
        r, c, anyb, hx, bad = m.groups()
        if bad is not None:
            raise GenError(f'bad token {bad!r} at offset {m.start(5)}')
        if c is not None:
            if not toks or toks[-1][0] != 'b' or toks[-1][1] not in (0xE8, 0xE9):
                raise GenError(f'C({c.strip()}) must follow a literal E8 or E9')
            toks.append(('c', c.strip()))
        elif r is not None:
            toks.append(('r', r.strip()))
        elif anyb:
            toks.append(('any',))
        else:
            toks.append(('b', int(hx, 16)))
        pos = m.end()
    if not toks:
        raise GenError('empty template')
    return toks


def operand_symbol(text, by_anchor, names):
    """R(...) operand -> symbol name. A name has an underscore (an optional VPX_ prefix is dropped); otherwise the text is
    a hex VA of the anchor build, looked up by anchor. Raises GenError when it names nothing in the catalogue."""
    t = text[4:] if text.startswith('VPX_') else text
    if '_' in t:
        if t not in names:
            raise GenError(f'operand R({text}) names no catalogue symbol')
        return t
    try:
        v = int(t, 16)
    except ValueError:
        raise GenError(f'operand R({text}) is neither a symbol name nor a hex VA')
    if v not in by_anchor:
        raise GenError(f'operand R({text}) is not the anchor of any catalogue symbol')
    return by_anchor[v]


def expected_bytes(toks, resolve, at_va=0):
    """[value or None] of a template with operands resolved (resolve(text) -> VA or None); E8/E9 followed by four literal
    or ?? tokens frees those four (callees and jump targets move with every link). C(x) after E8/E9 is the rel32 from the
    end of that instruction (at_va + its offset + 4) to x's VA: a call target is checked, not freed (ticket 4960c)."""
    exp, kinds = [], []
    for t in toks:
        if t[0] == 'b':
            exp.append(t[1])
            kinds.append('b')
        elif t[0] == 'any':
            exp.append(None)
            kinds.append('any')
        elif t[0] == 'c':
            v = resolve(t[1])
            if not v:
                raise GenError(f'call target C({t[1]}) is not carried to this build')
            rel = (v - (at_va + len(exp) + 4)) & 0xFFFFFFFF
            exp += list(struct.pack('<I', rel))
            kinds += ['c'] * 4
        else:
            v = resolve(t[1])
            exp += list(struct.pack('<I', v)) if v else [None] * 4
            kinds += ['r'] * 4
            if not v:
                raise GenError(f'operand R({t[1]}) is not carried to this build')
    i = 0
    while i < len(exp):
        if kinds[i] == 'b' and exp[i] in (0xE8, 0xE9) and i + 4 < len(exp) and \
                all(k in ('b', 'any') for k in kinds[i + 1:i + 5]):
            exp[i + 1:i + 5] = [None] * 4
            i += 5
        else:
            i += 1
    return exp


def check_patterns(patterns, pe, values, by_anchor, names):
    """[(file, name, at_symbol, result 'OK'|'BAD', reason)] for every pattern on the image `pe` with the values
    (name -> VA) of its build."""
    res = []
    for f, pname, at, tmpl in patterns:
        at_sym, reason = None, ''
        try:
            at_sym = operand_symbol(at[2:] if at.lower().startswith('0x') else at, by_anchor, names)
            va = values.get(at_sym)
            if not va:
                raise GenError(f'the pattern address {at_sym} is not carried to this build')
            exp = expected_bytes(parse_template(tmpl), lambda o: values.get(operand_symbol(o, by_anchor, names)), va)
            got = pe.read_rva(va - pe.image_base, len(exp))
            if got is None or len(got) < len(exp):
                raise GenError('the bytes at the pattern address are not in the file')
            bad = next((k for k, (e, g) in enumerate(zip(exp, got)) if e is not None and e != g), None)
            if bad is not None:
                raise GenError(f'mismatch at +{bad} of {len(exp)}')
        except GenError as e:
            reason = str(e)
        res.append((f, pname, at_sym or at, 'BAD' if reason else 'OK', reason))
    return res


# ------------------------------------------------------------------------------------------------------- carrying
D3DX_RE = re.compile(r'^(d3dx9|d3dx10|d3dx11|d3dcompiler)_4\d\.dll$', re.I)


def dll_key(dll):
    """d3dx9_42.dll and d3dx9_43.dll (and the other D3DX / D3DCompiler revisions) are one library for the slot."""
    d = dll.lower()
    m = D3DX_RE.match(d)
    return m.group(1).lower() + '_4x.dll' if m else d


def find_import(pe, dll, fn):
    """The IAT slot VA of dll!fn in pe (DLL equivalence by dll_key, names exact), or None."""
    want = dll_key(dll if dll.lower().endswith('.dll') else dll + '.dll')
    for d, lst in pe.imports.items():
        if dll_key(d or '') == want:
            for nm, slot in lst:
                if nm == fn:
                    return pe.image_base + slot
    return None


def section_named(pe, name):
    return next((s for s in pe.sections if s['name'] == name), None)


def blob_len(pe, va):
    """Length to compare for a .shader blob at va: the DXBC container's total size, else 32."""
    head = pe.read_rva(va - pe.image_base, 28)
    if head and len(head) == 28 and head[:4] == b'DXBC':
        n = struct.unpack_from('<I', head, 24)[0]
        if 32 <= n <= 0x200000:
            return n
    return 32


def old_section(old, va):
    return old.sec(va)


def check_anchor(old, s):
    """GenError when the anchor does not fit its kind on the anchor build."""
    want = SECTION_OF_KIND.get(s.kind)
    have = old.sec(s.anchor)
    if s.kind == 'data' and have not in ('.data', '.bss'):
        raise GenError(f'{s.name}: anchor in {have}, kind data wants .data/.bss ({s.where})')
    if s.kind != 'data' and have != want:
        raise GenError(f'{s.name}: anchor in {have}, kind {s.kind} wants {want} ({s.where})')
    if s.kind == 'iat':
        dll, fn = table.iat_parts(s.note)
        if find_import(old.pe, dll, fn) != s.anchor:
            raise GenError(f'{s.name}: the anchor is not the import slot of {dll}!{fn} in the anchor build ({s.where})')


def carry(old, new, s, shader_delta, am):
    """(va or None, method, votes, confidence) for one non-array symbol."""
    va = s.anchor
    if s.kind == 'code':
        return am.match_code(old, new, va)
    if s.kind in ('data', 'rdata'):
        return am.match_data(old, new, va)
    if s.kind == 'vtable':
        return carry_vtable(old, new, va, am)
    if s.kind == 'shader':
        if shader_delta is None:
            return None, 'no .shader', 0, ''
        nv = va + shader_delta
        n = blob_len(old.pe, va)
        a, b = old.pe.read_rva(va - old.base, n), new.pe.read_rva(nv - new.base, n)
        if a and b and a == b:
            return nv, 'shader-shift', n, 'high'
        return None, 'shader-shift: bytes differ', 0, ''
    if s.kind == 'iat':
        dll, fn = table.iat_parts(s.note)
        nv = find_import(new.pe, dll, fn)
        return (nv, 'import', 1, 'high') if nv else (None, 'import: not found', 0, '')
    raise GenError(f'kind {s.kind} cannot be carried as a single value')


def vtable_entries(img, va, limit=512):
    """The consecutive code pointers from va on (a vtable's entries; MSVC puts the next vtable's RTTI locator, a .rdata
    pointer, between two vtables, so the run ends there)."""
    out = []
    lo, hi = img.base + img.t_rva, img.base + img.t_end
    while len(out) < limit:
        e = img.pe.u32(va - img.base + 4 * len(out))
        if e is None or not (lo <= e < hi):
            break
        out.append(e)
    return out


_ENTRY_CACHE = {}


def entries_agree(old, new, ova, nva, am):
    """(agreeing, mapped): of the old vtable's entries that match_code carries, how many equal the new vtable's entry at
    the same index (and the two vtables have the same length)."""
    oe, ne = vtable_entries(old, ova), vtable_entries(new, nva)
    agree = mapped = 0
    for k, e in enumerate(oe):
        key = (id(old), id(new), e)
        if key not in _ENTRY_CACHE:
            r = am.match_code(old, new, e)
            _ENTRY_CACHE[key] = r[0] if r[3] in ('high', 'medium') else None
        m = _ENTRY_CACHE[key]
        if m:
            mapped += 1
            agree += k < len(ne) and ne[k] == m
    return agree, mapped, len(oe) == len(ne)


def carry_vtable(old, new, va, am):
    """A vtable: addr_match's entry vote (match_vtable) and the references to it (xrefs, the constructors' stores), with
    the entries as the arbiter. A derived class that keeps some of the base's entries ties the entry vote (FCamera,
    2026-11-19), so a candidate is taken only when every carried entry agrees and the lengths are equal."""
    v = am.match_vtable(old, new, va)
    x = am._match_data_xrefs(old, new, va)
    for cand, conf, how, votes in ((x[0], x[3], 'xrefs', x[2]), (v[0], v[3], 'vtable', v[2])):
        if not cand or conf not in ('high', 'medium'):
            continue
        agree, mapped, same_len = entries_agree(old, new, va, cand, am)
        if mapped >= 2 and agree == mapped and same_len:
            both = v[0] == x[0]
            return cand, how + ('+vtable' if both and how == 'xrefs' else '') + '+entries', \
                '%s;entries %d/%d' % (votes, agree, mapped), 'high'
    cand = x[0] or v[0]
    return cand, 'vtable: no candidate with all carried entries agreeing', '%s;%s' % (x[2], v[2]), 'low' if cand else ''


def carry_via_vtables(old, new, sym, resolved_vtables):
    """A code symbol that is entry k of a carried vtable: the new vtable's entry k. (va, method, votes, conf) or None."""
    for vname, ova, nva in resolved_vtables:
        oe = vtable_entries(old, ova)
        if sym.anchor in oe:
            k = oe.index(sym.anchor)
            ne = vtable_entries(new, nva)
            if len(ne) == len(oe):
                return ne[k], f'vtable-entry {vname}[{k}]', 1, 'high'
    return None


def span_equal(old, new, oa, ob, na, pad=8):
    """The .text bytes old[oa-pad, ob+pad) and new[na-pad, na+(ob-oa)+pad) are equal wherever neither image's mask (a
    relocated dword or a branch rel32) covers them, with at least 8 bytes compared. Offsets are into .text."""
    lo_o, hi_o, lo_n = oa - pad, ob + pad, na - pad
    if lo_o < 0 or lo_n < 0 or hi_o > len(old.text) or lo_n + (hi_o - lo_o) > len(new.text):
        return False
    fixed = 0
    for k in range(hi_o - lo_o):
        if old.mask[lo_o + k] or new.mask[lo_n + k]:
            continue
        if old.text[lo_o + k] != new.text[lo_n + k]:
            return False
        fixed += 1
    return fixed >= 8


def carry_by_row_siblings(old, new, cols, rows, cache, window=96):
    """Array cells pass 1 left (row_sibling, ticket 4966, 2026-11-19): a value is found at the same distance from a carried
    cell of its own row, in the same masked bytes.
      code cell X (e.g. a create call site): an instruction near X references a carried sibling Y (a blob VA, a handle
          global); in the new .text every reference to Y's new value is a candidate, X' = that reference + (X - ref),
          kept when the masked bytes between them are equal.
      data/rdata cell X (e.g. the handle global the created shader is stored in): an instruction within `window` bytes
          of a carried code sibling C references X; at C' + the same distance the new instruction's operand is X', kept
          when the masked bytes between are equal and X' is in the new .data.
    A value is taken only when every row it appears in that gives a candidate gives the same one. Returns
    {(kind, value): new value}."""
    base_o, base_n = old.base + old.t_rva, new.base + new.t_rva
    oxr, nxr = old.xrefs(), new.xrefs()
    votes = collections.defaultdict(set)

    def carried(kind, v):
        c = cache.get((kind, v))
        return c[0] if c else None

    for _round in range(2):   # code cells first, then data cells can use them
        for row in rows:
            for j, (kind, v) in enumerate(zip(cols, row)):
                if kind in ('keep', 'shader') or not v or carried(kind, v):
                    continue
                if kind == 'code':
                    xo = v - base_o
                    for k, (yk, y) in enumerate(zip(cols, row)):
                        if k == j or yk not in ('data', 'rdata', 'shader') or not y or not carried(yk, y):
                            continue
                        yn = carried(yk, y)
                        cands = set()
                        for r in oxr.get(y, []):
                            if abs(r - xo) > window:
                                continue
                            for rn in nxr.get(yn, []):
                                xn = rn + (xo - r)
                                if span_equal(old, new, min(r, xo), max(r, xo) + 4, min(rn, xn)):
                                    cands.add(base_n + xn)
                        # a sibling shared by look-alike code (one handle stored after two create calls) gives one
                        # candidate per copy: it cannot tell them apart and is not used
                        if len(cands) == 1:
                            votes[(kind, v)].add(next(iter(cands)))
                else:
                    for k, (ck, c) in enumerate(zip(cols, row)):
                        if ck != 'code' or not c or not carried(ck, c):
                            continue
                        co, cn = c - base_o, carried(ck, c) - base_n
                        cands = set()
                        for r in oxr.get(v, []):
                            if abs(r - co) > window:
                                continue
                            rn = cn + (r - co)
                            if not span_equal(old, new, min(r, co), max(r, co) + 4, min(rn, cn)):
                                continue
                            nv = struct.unpack_from('<I', new.text, rn)[0] if 0 <= rn <= len(new.text) - 4 else 0
                            if nv and sec_ok(new, kind, nv):
                                cands.add(nv)
                        if len(cands) == 1:
                            votes[(kind, v)].add(next(iter(cands)))
        out = {key: next(iter(vs)) for key, vs in votes.items() if len(vs) == 1}
        for key, nv in out.items():
            cache[key] = (nv, 'row-sibling')
    return {key: next(iter(vs)) for key, vs in votes.items() if len(vs) == 1}


def sec_ok(new, kind, nv):
    have = new.sec(nv)
    return have in ('.data', '.bss') if kind == 'data' else have == SECTION_OF_KIND.get(kind)


def hex_(v):
    return '0x%X' % v


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def read_hand_review(path, names, arrays):
    """{name: (va, reference)} and {(array, row, col): (va, reference)} from hand-review.tsv."""
    single, cells, skipped = {}, {}, []
    if not path.is_file():
        return single, cells
    for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        cells_ = [c.strip() for c in line.split('\t')]
        if cells_[0] == 'name':
            continue
        if len(cells_) < 3 or not cells_[2]:
            raise GenError(f'{path.name}:{n}: want name<TAB>va<TAB>research reference')
        name, v, ref = cells_[0], cells_[1], cells_[2]
        try:
            va = int(v, 16)
        except ValueError:
            raise GenError(f'{path.name}:{n}: va {v!r} is not hex')
        m = re.match(r'^([a-z0-9_]+)\[(\d+)\]\[(\d+)\]$', name)
        # ticket 4960b: the file is shared by every checkout, whose catalogues may differ (carriers add rows on their
        # own branches), so an answer for a symbol this catalogue does not have is skipped with a note, not refused
        if m and m.group(1) in arrays:
            cells[(m.group(1), int(m.group(2)), int(m.group(3)))] = (va, ref)
        elif not m and name in names:
            single[name] = (va, ref)
        elif re.match(r'^[a-z][a-z0-9_]*(\[\d+\]\[\d+\])?$', name):
            skipped.append(name)
        else:
            raise GenError(f'{path.name}:{n}: {name!r} is not a symbol name or name[row][col]')
    if skipped:
        print(f'gen_table: note: {path.name}: {len(skipped)} answer(s) for symbols not in this catalogue skipped: '
              f'{", ".join(skipped)}')
    return single, cells


def generate(a):
    import addr_match as am
    syms = table.symbols(a.symbols)
    names = {s.name for s in syms}
    by_anchor = {s.anchor: s.name for s in syms if s.anchor is not None}
    arrays = {s.name: s for s in syms if s.kind == 'array'}
    ob = builds.of_file(a.old)
    if ob is None or ob.id != builds.ANCHOR.id:
        raise GenError(f'--old {a.old} is not the DX11 EXE of the anchor build {builds.ANCHOR.steam_build} '
                       f'(stamp/SizeOfImage {builds.read_header(a.old)})')
    old = am.Img(str(a.old))
    if a.identity:
        sb, new, nb = builds.ANCHOR.steam_build, old, ob
    else:
        if not a.new or not a.steam_build:
            raise GenError('--new and --steam-build are needed (or --identity)')
        nb = builds.of_file(a.new)
        stamp, soi = builds.read_header(a.new)
        if stamp is None:
            raise GenError(f'--new {a.new}: not a 32-bit PE file')
        sb = str(a.steam_build)
        if nb is None:
            print(f'gen_table: note: --new (stamp 0x{stamp:08X}, SizeOfImage 0x{soi:08X}) is not in VPEXE_KNOWN_BUILDS: '
                  f'add build {sb} to exebuild/include/vp_exe_build.h and tools/exebuild/builds.py before building')
        elif nb.steam_build != sb:
            raise GenError(f'--new is build {nb.steam_build} by its header, --steam-build says {sb}')
        if nb is not None and nb.id == builds.ANCHOR.id:
            raise GenError('--new is the anchor build: use --identity')
        new = am.Img(str(a.new))
    tables = Path(a.tables) if a.tables else table.tables_dir()
    outdir = tables / sb
    print(f'gen_table: build {sb}{" (identity)" if a.identity else ""}: {len(syms)} catalogue symbols '
          f'({", ".join("%s %d" % kv for kv in sorted(collections.Counter(s.kind for s in syms).items()))})')

    for s in syms:
        if s.anchor is not None:
            check_anchor(old, s)
    so, sn = section_named(old.pe, '.shader'), section_named(new.pe, '.shader')
    shader_delta = (sn['rva'] - so['rva']) if so and sn else None

    hand, hand_cells = read_hand_review(outdir / 'hand-review.tsv', names, arrays)
    entries, review, unresolved = {}, [], []
    single = [s for s in syms if s.kind != 'array']
    found = {}
    for s in single:
        if a.identity:
            if s.kind == 'shader' and blob_len(old.pe, s.anchor) == 32 and \
                    (old.pe.read_rva(s.anchor - old.base, 4) or b'') != b'DXBC':
                raise GenError(f'{s.name}: no DXBC blob at the anchor ({s.where})')
            found[s.name] = (s.anchor, 'identity', '', 'identity')
        else:
            nv, how, votes, conf = carry(old, new, s, shader_delta, am)
            if nv and not sec_ok(new, s.kind, nv):
                how, conf = how + ' (carried into %s)' % new.sec(nv), 'low'
            found[s.name] = (nv, how, votes, conf)
    if not a.identity:
        # second pass: a code symbol that is an entry of a carried vtable (a destructor is often not unique by bytes)
        vt = [(s.name, s.anchor, found[s.name][0]) for s in single
              if s.kind == 'vtable' and found[s.name][3] == 'high']
        for s in single:
            if s.kind == 'code' and found[s.name][3] not in ('high', 'medium'):
                r = carry_via_vtables(old, new, s, vt)
                if r and sec_ok(new, 'code', r[0]):
                    found[s.name] = r
    for s in single:
        nv, how, votes, conf = found[s.name]
        if s.name in hand:
            hv, ref = hand[s.name]
            if nv and conf in ('high', 'medium') and hv != nv:
                print(f'gen_table: note: {s.name}: the hand-review answer overrides a {conf} match that differs')
            nv, how, votes, conf = hv, 'hand: ' + ref, '', 'hand'
        if nv and conf in TAKEN:
            entries[s.name] = {'va': hex_(nv), 'kind': s.kind, 'method': how, 'votes': str(votes), 'confidence': conf}
        else:
            unresolved.append(s.name)
            review.append((s.name, s.kind, hex_(s.anchor), hex_(nv) if nv else '', how, str(votes), conf or 'none'))

    arr_out, arr_missing = {}, {}
    cache = {}
    for name, s in arrays.items():
        rows = table.parse_array_file(table.REPO / s.array_file, len(s.array_cols))
        # pass 1: every address cell by its column's kind (one carry per distinct value)
        for i, row in enumerate(rows):
            for j, (kind, v) in enumerate(zip(s.array_cols, row)):
                if kind == 'keep' or not v:
                    continue
                sec = old.sec(v)
                okk = (sec in ('.data', '.bss')) if kind == 'data' else sec == SECTION_OF_KIND[kind]
                if not okk:
                    raise GenError(f'{name}[{i}][{j}]: the anchor is in {sec}, column kind {kind} ({s.where})')
                key = (kind, v)
                if key in cache:
                    continue
                if a.identity:
                    cache[key] = (v, 'identity')
                    continue
                tmp = table.Symbol(f'{name}_cell', kind, v, '', '', '', '')
                r = carry(old, new, tmp, shader_delta, am)
                ok = r[0] and r[3] in ('high', 'medium') and sec_ok(new, kind, r[0])
                cache[key] = (r[0], r[1]) if ok else None
        # pass 2: a value pass 1 could not carry, found next to a carried cell of the same row (row_sibling)
        sib = {}
        if not a.identity:
            sib = carry_by_row_siblings(old, new, s.array_cols, rows, cache)
            for key, nv in sib.items():
                cache[key] = (nv, 'row-sibling')
        out_rows, miss, by_sibling = [], 0, len(sib)
        for i, row in enumerate(rows):
            orow = []
            for j, (kind, v) in enumerate(zip(s.array_cols, row)):
                if kind == 'keep':
                    orow.append(v)
                    continue
                c = cache.get((kind, v)) if v else (0, '')
                nv = c[0] if c else None
                if (name, i, j) in hand_cells:
                    nv = hand_cells[(name, i, j)][0]
                if not nv and v:
                    miss += 1
                    review.append((f'{name}[{i}][{j}]', kind, hex_(v), '', 'array cell', '', 'none'))
                orow.append(hex_(nv) if nv else 0)
            out_rows.append(orow)
        arr_out[name] = {'file': s.array_file, 'cols': list(s.array_cols), 'rows': out_rows, 'unresolved_cells': miss,
                         'values_by_row_sibling': by_sibling}
        arr_missing[name] = miss
        print(f'gen_table: array {name}: {len(rows)} rows x {len(s.array_cols)} columns, {miss} address cell(s) '
              f'not carried ({by_sibling} value(s) carried by a row sibling)')

    # patterns, checked on the build the table describes, with its own values
    values = {k: int(e['va'], 16) for k, e in entries.items()}
    pats = read_pattern_tables(pattern_files(a.patterns or ()))
    pres = check_patterns(pats, new.pe, values, by_anchor, names)
    allowed = set(a.allow_bad or ())
    bad = [r for r in pres if r[3] == 'BAD']
    refused = [r for r in bad if r[1] not in allowed and f'{r[0]}:{r[1]}' not in allowed]
    for f, pname, at, res, reason in pres:
        print(f'  {res:3} {f}: {pname} [{at}]' + (f' - {reason}' if reason else ''))
    print(f'gen_table: patterns {len(pres) - len(bad)} of {len(pres)} OK' +
          (f'; BAD allowed by --allow-bad: {len(bad) - len(refused)}' if len(bad) != len(refused) else ''))

    conf_counts = collections.Counter(e['confidence'] for e in entries.values())
    print(f'gen_table: symbols carried {len(entries)} of {len(syms) - len(arrays)} '
          f'({", ".join("%s %d" % kv for kv in sorted(conf_counts.items()))}); unresolved {len(unresolved)}'
          + (': ' + ', '.join(unresolved) if unresolved else ''))

    outdir.mkdir(parents=True, exist_ok=True)
    rv = outdir / 'review.tsv'
    with open(rv, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# review.tsv: LOCAL ONLY (D202). Symbols the matcher did not carry with high or medium confidence,\n'
                '# with the candidate if any. Answer each in hand-review.tsv (name<TAB>va<TAB>research reference).\n')
        f.write('name\tkind\tanchor_va\tcandidate_va\tmethod\tvotes\tconfidence\n')
        for r in review:
            f.write('\t'.join(r) + '\n')
    if refused:
        print(f'gen_table: REFUSED: {len(refused)} BAD pattern(s) not allowed ({", ".join(r[1] for r in refused)}); '
              f'nothing written to {outdir / "table.json"} (review.tsv written). Fix the catalogue or hand-review.tsv, '
              f'or name a pattern with --allow-bad after review')
        return 1
    complete = not unresolved and not any(arr_missing.values()) and not bad
    doc = {
        'format': 1,
        'note': 'LOCAL ONLY (D202): never commit, push or quote these values',
        'steam_build': sb,
        'build_known': nb is not None,
        'stamp': '0x%08X' % builds.read_header(a.old if a.identity else a.new)[0],
        'size_of_image': '0x%08X' % builds.read_header(a.old if a.identity else a.new)[1],
        'anchor_build': builds.ANCHOR.steam_build,
        'generated': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
        'generator': 'tools/exebuild/gen_table.py',
        'mode': 'identity' if a.identity else 'carried',
        'old_exe_sha256': sha256(a.old),
        'new_exe_sha256': sha256(a.old if a.identity else a.new),
        'catalogue_sha256': table.catalogue_hash(a.symbols),
        'symbols': entries,
        'unresolved': unresolved,
        'arrays': arr_out,
        'patterns': [{'file': f, 'name': n, 'at': at, 'result': r, 'reason': why} for f, n, at, r, why in pres],
        'complete': complete,
    }
    tp = outdir / 'table.json'
    text = json.dumps(doc, indent=1) + '\n'
    # ticket 4960b: also as table-<catalogue hash16>.json, so checkouts with other catalogues keep their own table
    for p in (tp, table.catalogue_table_path(sb, doc['catalogue_sha256'], tables)):
        tmp = p.with_suffix('.json.tmp')
        tmp.write_text(text, encoding='utf-8')
        os.replace(tmp, p)
    print(f'gen_table: wrote {tp} and {table.catalogue_table_path(sb, doc["catalogue_sha256"], tables).name} '
          f'({"complete" if complete else "INCOMPLETE: see review.tsv"})')
    return 0 if complete else 4


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', type=Path, required=True, help='DX11 EXE of the anchor build 4390913 (read only)')
    ap.add_argument('--new', type=Path, help='DX11 EXE of the build to carry to (read only)')
    ap.add_argument('--steam-build', help='Steam build id of --new (the table folder name)')
    ap.add_argument('--identity', action='store_true', help="write the anchor build's own table")
    ap.add_argument('--tables', type=Path, help='table folder (default: the setting exe_tables / C5_EXE_TABLES)')
    ap.add_argument('--patterns', action='append', help='another file with an X(...) pattern table (repeatable)')
    ap.add_argument('--allow-bad', action='append', metavar='NAME', help='write although this pattern is BAD')
    ap.add_argument('--symbols', type=Path, help=argparse.SUPPRESS)   # tests: another catalogue folder
    a = ap.parse_args(argv)
    for p in [a.old] + ([a.new] if a.new else []):
        if not p.is_file():
            print(f'gen_table: FAILED: {p} not found')
            return 2
    try:
        return generate(a)
    except (GenError, table.CatalogueError, table.TableError) as e:
        print(f'gen_table: FAILED: {e}')
        return 2


if __name__ == '__main__':
    sys.exit(main())
