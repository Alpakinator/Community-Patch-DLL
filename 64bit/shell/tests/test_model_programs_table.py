"""Ticket 4874: shell/common/shell_model_scene.h (the model filter's program table) against the committed evidence it came
from, and a mutated copy caught (as render/tests/units/test_unit_programs_table.cpp does for the units' table):
  - every program's full SHA-256 and VA equal render/evidence/0561/4874/p5_sites.txt (the EXE's blob at its one create
    site, equal to the 3910 capture's DXBC file);
  - stage, materials and pass equal 4870's render/evidence/0561/4870/programs.csv;
  - the DROP pairs and their classes equal 4870's filter_table_draft.csv (the drop+tap rows: VS, PS, pass 0), and every
    KEEP program (pass 1) is a mask row's program; no DROP program is a pass-1 program;
  - unit_filter.cpp's site table rows 12..21 equal p5_sites.txt (blob, push, call, store, handle global). Since epic
    0590's ticket 4965 (main 7cb3e57f8, 2026-11-20) the rows name build-table symbols (VPX_uf_*_blob/_push/_handle and
    a store offset; the call at push + UF_CALL_OFF): the symbols' anchors in exebuild/symbols/shell.tsv (build 4390913),
    the offsets and each row's run-time pattern are what is checked (ticket 0627; before it the old literal-row pattern
    matched nothing and this test was red);
  - no model program is a unit program (shell/common/unit_programs.h).
The server's table (4873) is to include shell_model_scene.h or extend this check. No build, no game: plain Python.
"""
import csv
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "shell" / "common" / "shell_model_scene.h"
UNITS = ROOT / "shell" / "common" / "unit_programs.h"
FILTER = ROOT / "shell" / "composite" / "unit_filter.cpp"
SYMS = ROOT / "exebuild" / "symbols" / "shell.tsv"
P5 = ROOT / "render" / "evidence" / "0561" / "4874" / "p5_sites.txt"
PROGRAMS = ROOT / "render" / "evidence" / "0561" / "4870" / "programs.csv"
DRAFT = ROOT / "render" / "evidence" / "0561" / "4870" / "filter_table_draft.csv"
MAT_BITS = {1: 1, 3: 2, 4: 4, 5: 8, 6: 16}
CLASS = {"0": "block", "1": "single", "2": "inst", "3": "skinned"}
DRAFT_CLASS = {"block": "block", "single": "single", "inst": "inst", "skinned": "skinned"}


def parse_scene(text):
    progs = []
    for m in re.finditer(r"\{\s*\{\s*((?:0x[0-9a-f]{2}\s*,\s*){31}0x[0-9a-f]{2})\s*\}\s*,\s*(0x[0-9A-Fa-f]+)u,\s*VPMODEL_STAGE_(VS|PS),\s*"
                         r"([A-Z0-9_ |]+),\s*(\d+),\s*0\s*\}", text):
        digest = "".join(b[2:] for b in re.findall(r"0x[0-9a-f]{2}", m.group(1)))
        mats = sum({"VPMODEL_MAT%d" % k: v for k, v in MAT_BITS.items()}[t.strip()] for t in m.group(4).split("|"))
        progs.append(dict(sha=digest, va=int(m.group(2), 16), stage=m.group(3).lower(), mats=mats, pass_=int(m.group(5))))
    pairs = [dict(cls=c, vs=int(v), ps=int(p), bit=int(b, 16))
             for c, v, p, b in re.findall(r"\{\s*VPMODEL_CLASS_(\w+),\s*(\d+),\s*(\d+),\s*(0x[0-9a-f]+|0)u?\s*\}", text)]
    return progs, pairs


def p5_rows():
    rows = {}
    for ln in P5.read_text().splitlines():
        m = re.match(r"(VS|PS) (\w{8}) va=0x([0-9A-F]+) size=(\d+) set=50 push=0x([0-9A-F]+) call=0x([0-9A-F]+) store=0x([0-9A-F]+) "
                     r"handle=0x([0-9A-F]+) create=0x([0-9A-F]+) bytes_ok=True sha256=(\w{64})", ln)
        if m:
            rows[m.group(2)] = dict(stage=m.group(1).lower(), va=int(m.group(3), 16), push=int(m.group(5), 16), call=int(m.group(6), 16),
                                    store=int(m.group(7), 16), handle=int(m.group(8), 16), sha=m.group(10))
    assert "ALL OK" in P5.read_text()
    return rows


def parse_symbols(text):
    """exebuild/symbols/shell.tsv -> {name: (kind, anchor VA)} (tab-separated, '#' comments, a header row)."""
    syms = {}
    for ln in text.splitlines():
        cols = ln.split("\t")
        if ln.startswith("#") or len(cols) < 3 or cols[0] == "name":
            continue
        syms[cols[0]] = (cols[1], int(cols[2], 16) if cols[2].startswith("0x") else None)
    return syms


def pattern_offsets(tokens):
    """the byte offsets of the first 'E8' and of '66 A3' in a pattern template (R(...) = 4 bytes, '??'/hex = 1)."""
    off, call, store = 0, None, None
    for i, t in enumerate(tokens):
        if t == "E8" and call is None:
            call = off
        if t == "66" and i + 1 < len(tokens) and tokens[i + 1] == "A3" and store is None:
            store = off
        off += 4 if t.startswith("R(") else 1
    return call, store


def check_sites(progs, p5, filt, syms_text):
    """unit_filter.cpp's site rows 12..21 (kSites, since ticket 4965 VPX_ symbols of the build table) against
    p5_sites.txt: each row names its own program, the rows stand in shell_model_scene.h order, the symbols' anchors in
    exebuild/symbols/shell.tsv (build 4390913) are the blob VA, push and handle global of p5, push + storeOff is p5's
    handle store, push + UF_CALL_OFF is p5's create call, and the row's run-time pattern puts its E8 and 66 A3 there."""
    probs = []
    syms = parse_symbols(syms_text)
    rows = re.findall(r"\{ VPX_uf_(vs|ps)_(\w{8})_blob, VPX_uf_(vs|ps)_(\w{8})_push, VPX_uf_(vs|ps)_(\w{8})_handle, 0x([0-9a-f]+)u \},"
                      r"\s*// (VS|PS) (\w{8})", filt)
    msites = [r for r in rows if r[8] in p5]
    if len(msites) != len(progs):
        return ["%d model site rows parsed in unit_filter.cpp's kSites, %d expected" % (len(msites), len(progs))]
    if [r[8] for r in msites] != [p["sha"][:8] for p in progs]:
        probs.append("unit_filter.cpp's model site rows are not in shell_model_scene.h order: %s" % [r[8] for r in msites])
    m = re.search(r"#define UF_CALL_OFF 0x([0-9A-Fa-f]+)u", filt)
    call_off = int(m.group(1), 16) if m else None
    if call_off is None:
        probs.append("unit_filter.cpp: no UF_CALL_OFF")
    pats = {pm.group(1): pm.group(2).split()
            for pm in re.finditer(r'X\(VPX_(uf_(?:vs|ps)_\w{8}_push), "[^"]*", "([^"]*)"\)', filt)}
    for r in msites:
        s8, e = r[8], p5[r[8]]
        base = "uf_%s_%s" % (r[0], s8)
        if r[1::2][:3] != (s8, s8, s8) or not (r[0] == r[2] == r[4] == r[7].lower()):
            probs.append("unit_filter.cpp site row of %s %s names another program's symbols" % (r[7], s8))
            continue
        if r[7].lower() != e["stage"]:
            probs.append("unit_filter.cpp site of %s: stage %s, p5_sites.txt %s" % (s8, r[7], e["stage"].upper()))
        want = {"_blob": ("shader", e["va"]), "_push": ("code", e["push"]), "_handle": ("data", e["handle"])}
        for suf, (kind, va) in want.items():
            got = syms.get(base + suf)
            if got != (kind, va):
                probs.append("shell.tsv %s%s: %s, p5_sites.txt (%s, 0x%X)" % (base, suf, got and (got[0], got[1] and "0x%X" % got[1]), kind, va))
        store_off = int(r[6], 16)
        if e["push"] + store_off != e["store"]:
            probs.append("unit_filter.cpp site of %s: storeOff 0x%X, p5_sites.txt 0x%X" % (s8, store_off, e["store"] - e["push"]))
        if call_off is not None and e["push"] + call_off != e["call"]:
            probs.append("unit_filter.cpp: UF_CALL_OFF 0x%X, p5_sites.txt has the call of %s at push+0x%X" % (call_off, s8, e["call"] - e["push"]))
        toks = pats.get(base + "_push")
        if not toks:
            probs.append("unit_filter.cpp: no site pattern for %s" % base)
            continue
        if toks[:2] != ["68", "R(%s_blob)" % base] or "R(%s_handle)" % base not in toks:
            probs.append("unit_filter.cpp site pattern of %s does not push its blob or store its handle" % s8)
        pc, ps = pattern_offsets(toks)
        if pc != e["call"] - e["push"] or ps != e["store"] - e["push"]:
            probs.append("unit_filter.cpp site pattern of %s: call +%s store +%s, p5_sites.txt +0x%X +0x%X"
                         % (s8, pc, ps, e["call"] - e["push"], e["store"] - e["push"]))
    return probs


def check(text, filt=None, syms=None):
    """-> list of problems (empty = the table agrees with the evidence). filt / syms: unit_filter.cpp / shell.tsv text
    (default: the files on disk)."""
    probs = []
    progs, pairs = parse_scene(text)
    if len(progs) != 10:
        return ["%d programs parsed, 10 expected" % len(progs)]
    p5 = p5_rows()
    csvp = {}
    for r in csv.DictReader(PROGRAMS.open()):
        if r["materials"] and "BuildingShader" in r["materials"]:
            csvp[r["sha8"]] = r
    for i, p in enumerate(progs):
        s8 = p["sha"][:8]
        if s8 not in p5:
            probs.append("program %d %s: not in p5_sites.txt" % (i, s8))
            continue
        e = p5[s8]
        if p["sha"] != e["sha"]:
            probs.append("program %d %s: digest differs from p5_sites.txt" % (i, s8))
        if p["va"] != e["va"] or p["stage"] != e["stage"]:
            probs.append("program %d %s: VA or stage differs from p5_sites.txt" % (i, s8))
        c = csvp.get(s8)
        if not c:
            probs.append("program %d %s: not a BuildingShader program of programs.csv" % (i, s8))
            continue
        mats, passes = 0, set()
        for item in c["materials"].split():
            mat, _name, ps = item.split(":")
            mats |= MAT_BITS[int(mat)]
            passes.add(int(ps))
        if int(c["va"], 16) != p["va"] or c["stage"] != p["stage"]:
            probs.append("program %d %s: VA or stage differs from programs.csv" % (i, s8))
        if mats != p["mats"] or passes != {p["pass_"]}:
            probs.append("program %d %s: materials 0x%x pass %d, programs.csv 0x%x %s" % (i, s8, p["mats"], p["pass_"], mats, sorted(passes)))
        if (i < 5) != (p["pass_"] == 0):
            probs.append("program %d %s: DROP programs are 0..4 (pass 0), KEEP 5..9 (pass 1)" % (i, s8))
    # the pairs against the draft's drop+tap rows (scene pass)
    want = set()
    keep = set()
    for r in csv.DictReader(DRAFT.open()):
        if r["proposed_action"].startswith("drop") and r["pass"] == "scene":
            want.add((DRAFT_CLASS[r["class"]], r["vs"], r["ps"]))
        if r["pass"].startswith("mask") and r["proposed_action"].startswith("keep"):
            keep.update([r["vs"], r["ps"]])
    got = set()
    for q in pairs:
        if max(q["vs"], q["ps"]) >= len(progs):
            probs.append("pair %s: index out of range" % q)
            continue
        got.add((q["cls"].lower(), progs[q["vs"]]["sha"][:8], progs[q["ps"]]["sha"][:8]))
        if progs[q["vs"]]["pass_"] or progs[q["ps"]]["pass_"]:
            probs.append("pair %s: a pass-1 program in a DROP pair" % q)
        bit = {"block": 2, "single": 2, "inst": 4, "skinned": 0}[q["cls"].lower()]
        if q["bit"] != bit:
            probs.append("pair %s: bit 0x%x, expected 0x%x" % (q, q["bit"], bit))
    if got != want:
        probs.append("DROP pairs %s differ from the draft's %s" % (sorted(got), sorted(want)))
    for p in progs[5:]:
        if p["sha"][:8] not in keep:
            probs.append("KEEP program %s is no mask program of the draft" % p["sha"][:8])
    probs += check_sites(progs, p5, FILTER.read_text() if filt is None else filt, SYMS.read_text() if syms is None else syms)
    units = UNITS.read_text()
    ud = {"".join(b[2:] for b in re.findall(r"0x[0-9a-f]{2}", m)) for m in re.findall(r"\{\s*\{\s*((?:0x[0-9a-f]{2}\s*,\s*){31}0x[0-9a-f]{2})", units)}
    for p in progs:
        if p["sha"] in ud:
            probs.append("model program %s is also a unit program" % p["sha"][:8])
    return probs


def test_the_table_equals_the_evidence():
    assert check(SCENE.read_text()) == []


def _mutate(text, old, new, count=1):
    assert old in text, old
    return text.replace(old, new, count)


@pytest.mark.parametrize("what,old,new", [
    ("digest byte", "0x7e,0xc4,0xe9,0xa5,0xaa", "0x7e,0xc4,0xe9,0xa5,0xab"),
    ("VA", "0x61A7D00u", "0x61A7D04u"),
    ("stage", "0x61A56E8u, VPMODEL_STAGE_PS", "0x61A56E8u, VPMODEL_STAGE_VS"),
    ("material bit", "VPMODEL_STAGE_PS, VPMODEL_MAT5, 0, 0", "VPMODEL_STAGE_PS, VPMODEL_MAT4, 0, 0"),
    ("pass", "0x61A0CB0u, VPMODEL_STAGE_VS, VPMODEL_MAT6, 0, 0", "0x61A0CB0u, VPMODEL_STAGE_VS, VPMODEL_MAT6, 1, 0"),
    ("pair index", "{ VPMODEL_CLASS_INST, 0, 2, 0x4u }", "{ VPMODEL_CLASS_INST, 3, 2, 0x4u }"),
    ("pair bit", "{ VPMODEL_CLASS_BLOCK, 0, 1, 0x2u }", "{ VPMODEL_CLASS_BLOCK, 0, 1, 0x4u }"),
    ("mask program in a pair", "{ VPMODEL_CLASS_SINGLE, 3, 2, 0x2u }", "{ VPMODEL_CLASS_SINGLE, 8, 2, 0x2u }"),
])
def test_a_mutated_table_is_caught(what, old, new):
    probs = check(_mutate(SCENE.read_text(), old, new))
    assert probs, "the mutation (%s) was not caught" % what


def test_the_site_rows_are_parsed():
    """the check that went red after 4965 must never pass by parsing nothing: all ten model rows and patterns are read."""
    progs, _ = parse_scene(SCENE.read_text())
    filt = FILTER.read_text()
    assert check_sites(progs, p5_rows(), filt, SYMS.read_text()) == []
    assert check_sites(progs, p5_rows(), "", SYMS.read_text()) == ["0 model site rows parsed in unit_filter.cpp's kSites, 10 expected"]
    assert len(re.findall(r"\{ VPX_uf_\w+_blob, ", filt)) == 22


@pytest.mark.parametrize("what,old,new", [
    ("store offset", "VPX_uf_ps_c704c80b_handle, 0x20u }", "VPX_uf_ps_c704c80b_handle, 0x21u }"),
    ("call offset", "#define UF_CALL_OFF 0x0Bu", "#define UF_CALL_OFF 0x0Cu"),
    ("row names another program", "{ VPX_uf_vs_7a9e17f6_blob, VPX_uf_vs_7a9e17f6_push, VPX_uf_vs_7a9e17f6_handle",
     "{ VPX_uf_vs_7a9e17f6_blob, VPX_uf_vs_88c2b7d4_push, VPX_uf_vs_7a9e17f6_handle"),
    ("row order", "{ VPX_uf_ps_c704c80b_blob, VPX_uf_ps_c704c80b_push, VPX_uf_ps_c704c80b_handle, 0x20u },   // PS c704c80b (5)",
     "{ VPX_uf_ps_f0e1f0a6_blob, VPX_uf_ps_f0e1f0a6_push, VPX_uf_ps_f0e1f0a6_handle, 0x20u },   // PS f0e1f0a6 (5)"),
    ("pattern handle store moved", '"68 R(uf_vs_34b98e63_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ??',
     '"68 R(uf_vs_34b98e63_blob) ?? ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? ??'),
    ("pattern call moved", '"68 R(uf_ps_e12e9eaf_blob) ?? ?? ?? ?? ?? ?? E8', '"68 R(uf_ps_e12e9eaf_blob) ?? ?? ?? ?? ?? E8 ??'),
    ("pattern pushes another blob", '"68 R(uf_vs_0a3f2230_blob)', '"68 R(uf_vs_34b98e63_blob)'),
    ("pattern row missing", 'X(VPX_uf_vs_c4bb25da_push,', 'X(VPX_uf_vs_c4bb25dX_push,'),
])
def test_a_mutated_site_row_is_caught(what, old, new):
    probs = check(SCENE.read_text(), filt=_mutate(FILTER.read_text(), old, new))
    assert probs, "the mutation (%s) was not caught" % what


@pytest.mark.parametrize("what,old,new", [
    ("blob anchor", "uf_vs_7ec4e9a5_blob\tshader\t0x619F1E8", "uf_vs_7ec4e9a5_blob\tshader\t0x619F1EC"),
    ("push anchor", "uf_ps_c704c80b_push\tcode\t0x4EBCB4", "uf_ps_c704c80b_push\tcode\t0x4EBCB5"),
    ("handle anchor", "uf_vs_7ec4e9a5_handle\tdata\t0x2DE158C", "uf_vs_7ec4e9a5_handle\tdata\t0x2DE1590"),
    ("kind", "uf_ps_c704c80b_handle\tdata\t", "uf_ps_c704c80b_handle\trdata\t"),
    ("symbol missing", "uf_ps_c704c80b_blob\tshader\t", "uf_ps_c704c80b_blobx\tshader\t"),
])
def test_a_mutated_symbol_is_caught(what, old, new):
    probs = check(SCENE.read_text(), syms=_mutate(SYMS.read_text(), old, new))
    assert probs, "the mutation (%s) was not caught" % what
