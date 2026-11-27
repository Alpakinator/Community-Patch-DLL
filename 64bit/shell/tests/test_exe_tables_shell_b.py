"""Ticket 4965 (epic 0590, D202): the EXE build table of the composite's decal, cut, forest, fog-model, unit-filter and
farm-guard taps. Written 2026-11-20 for this repository (GPL-3.0-or-later).

Static checks, no game; the EXEs read are the golden copies only (tools/harness/exe_build.golden_install, read only; the
Steam install is never read for tests, epic 0590):
- the six files hold no stamp and no EXE VA literal outside their X(VPX_name, "name", "bytes") pattern rows, and every row
  names its address by a catalogue symbol (exebuild/symbols/shell.tsv);
- every row is OK in the local table of every known build (tools/exebuild/gen_table.py checked it against that build's
  golden EXE when it wrote the table; research/02-exe-internals/exe-build-table.md, section "Shell carry B"), and again
  here against the golden EXE file of each build with the table's values; the tables are local only (setting exe_tables
  / C5_EXE_TABLES), and their absence fails naming the setting, here and in the build (tools/exebuild/emit_header.py,
  shell/build.cmd step 0a).
Nothing of a table is printed: a failure names the file, the pattern and its symbol only.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

import shellrun

REPO = shellrun.REPO
sys.path.insert(0, str(REPO))
from tools.exebuild import builds, gen_table, pe32, table  # noqa: E402
from tools.harness import exe_build  # noqa: E402

FILES = ["shell/composite/%s.cpp" % f for f in ("decal_tap", "cut_tap", "forest_tap", "fow_models", "unit_filter", "farm_guard")]
HEADERS = ["shell/composite/%s.h" % f for f in ("decal_tap", "cut_tap", "forest_tap", "fow_models", "unit_filter", "farm_guard")]
X_RE = re.compile(r'X\(\s*([A-Za-z0-9_]+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"([^"]*)"\s*\)')
# the rows each file must hold (a row lost in an edit fails here, not silently in gen_table)
ROWS = {"decal_tap": 45, "cut_tap": 13, "forest_tap": 12, "fow_models": 7, "unit_filter": 32, "farm_guard": 39}   # farm_guard: 13 + ticket 0655's five post rows and three area-probe rows + ticket 0657's 13 edge rows (count set by ticket 0603, 2026-11-24) + ticket 0784's five tread rows


def text(rel):
    return (REPO / rel).read_text(encoding="utf-8")


@pytest.mark.parametrize("rel", FILES + HEADERS + ["shell/composite/shell_exe.h"])
def test_no_stamp_and_no_va_literal(rel):
    """Outside the pattern rows: no PE time stamp of a known build and no hex literal in the EXE's address range
    (0x400000 .. 0x6900000, both builds' images; small offsets and masks are allowed)."""
    src = X_RE.sub("", text(rel))
    for b in builds.KNOWN:
        assert ("%08X" % b.stamp) not in src.upper(), (rel, "stamp of build", b.steam_build)
    bad = []
    for m in re.finditer(r"0x([0-9A-Fa-f]{6,8})\b", src):
        v = int(m.group(1), 16)
        if 0x400000 <= v < 0x6900000:
            line = src.count("\n", 0, m.start()) + 1
            bad.append((line, m.group(0)))
    # values in that range that are not addresses: sizes and limits the files use
    allowed = {0x400000, 0x800000, 0x4000000, 0x100000, 0x7FFF8, 0x1000000,   # 0x400000: the ImageBase
               0x800180}   # ticket 0655: the 4 MB queue's channel stride (farm_guard: the farm-end post's queue object)
    bad = [b for b in bad if int(b[1], 16) not in allowed]
    assert not bad, (rel, bad)


@pytest.mark.parametrize("rel", FILES)
def test_rows_name_catalogue_symbols(rel):
    names = table.by_name()
    rows = X_RE.findall(text(rel))
    assert len(rows) == ROWS[Path(rel).stem], (rel, len(rows))
    for at, name, tmpl in rows:
        assert at.startswith("VPX_") and at[4:] in names, (rel, name, at)
        for op in re.findall(r"R\(([^)]*)\)", tmpl):
            assert "_" in op and op.replace("VPX_", "") in names, (rel, name, op)   # names, never hex VAs


@pytest.mark.parametrize("build", [b.steam_build for b in builds.KNOWN])
def test_every_row_ok_in_the_local_table(build):
    """The local table of the build (generated from this catalogue) has every row of the six files OK and is complete."""
    t = table.load(build, chash=table.catalogue_hash())   # 4960b: this catalogue's own copy (table-<hash16>.json)
    assert t.get("catalogue_sha256") == table.catalogue_hash(), \
        "the local table of %s was made from another catalogue: run tools/exebuild/gen_table.py again" % build
    assert t.get("complete") is True, (build, "incomplete table", t.get("unresolved"))
    for rel in FILES:
        got = [p for p in t["patterns"] if p["file"] == rel]
        assert len(got) == ROWS[Path(rel).stem], (build, rel, len(got))
        bad = [(p["name"], p["at"], p["reason"]) for p in got if p["result"] != "OK"]
        assert not bad, (build, rel, bad)


@pytest.mark.parametrize("build", [b.steam_build for b in builds.KNOWN])
def test_every_row_matches_the_golden_exe(build):
    """Every row of the six files against the golden EXE file of the build (read only), its operands and its address
    taken from this catalogue's local table (the anchor build: the catalogue's anchors), with gen_table's own rules.
    A missing golden copy or table fails naming it (no skip)."""
    exe = exe_build.golden_install(build) / exe_build.EXE_NAME
    assert exe.is_file(), "the golden copy of build %s is missing: %s" % (build, exe)
    t = table.load(build, chash=table.catalogue_hash())
    values = {n: int(e["va"], 16) for n, e in t["symbols"].items() if e.get("va")}
    syms = table.symbols()
    names = {s.name for s in syms}
    by_anchor = {s.anchor: s.name for s in syms if s.anchor is not None}
    pats = gen_table.read_pattern_tables(FILES)
    assert len(pats) == sum(ROWS.values()), len(pats)
    pe = pe32.PE(str(exe))
    res = gen_table.check_patterns(pats, pe, values, by_anchor, names)
    bad = [(f, name, at, why) for f, name, at, ok, why in res if ok != "OK"]
    assert not bad, (build, bad)
    # the negative: one literal byte of a row changed is BAD there (the check can fail)
    f, name, at, tmpl = pats[0]
    tok = re.search(r"(?<![(\w])([0-9A-F]{2})(?![\w)])", tmpl)
    broken = tmpl[:tok.start()] + "%02X" % (int(tok.group(1), 16) ^ 0x5A) + tmpl[tok.end():]
    res = gen_table.check_patterns([(f, name, at, broken)], pe, values, by_anchor, names)
    assert res[0][3] == "BAD" and "mismatch" in res[0][4], res


def test_listed_in_patterns_txt():
    listed = [ln.split("#")[0].strip() for ln in text("exebuild/patterns.txt").splitlines()]
    for rel in FILES:
        assert rel in listed, rel


def test_missing_tables_fail_naming_the_setting(tmp_path):
    """No table: the reader and the build's emitter fail and name exe_tables / C5_EXE_TABLES (exit 2), nothing emitted."""
    with pytest.raises(table.TableError) as e:
        table.load("26577625", tables=tmp_path)
    assert "exe_tables" in str(e.value) and "C5_EXE_TABLES" in str(e.value)
    env = dict(os.environ, C5_EXE_TABLES=str(tmp_path))
    out = tmp_path / "gen"
    cp = subprocess.run([sys.executable, "-I", str(REPO / "tools" / "exebuild" / "emit_header.py"), "--out", str(out)],
                        capture_output=True, text=True, env=env, cwd=str(REPO))
    msg = cp.stdout + cp.stderr
    assert cp.returncode == 2, msg
    assert "exe_tables" in msg and "C5_EXE_TABLES" in msg, msg
    assert not (out / "vp_exe_tables.gen.h").exists()
