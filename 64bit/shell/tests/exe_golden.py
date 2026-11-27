"""Ticket 4964 (epic 0590, D202): the shell tests' view of the two known EXE builds.

- golden(build): the DX11 EXE of a build's golden copy (read only; F:/c5-64bit-work/golden/... on the workstation,
  tools/harness/exe_build.golden_install), never the owner's Steam install. Missing: the test FAILS naming the folder.
- check_patterns(file): every X(symbol, "description", "bytes") row of a source file, expanded with the build's carried
  values from the local build table (tools/exebuild/table.py; setting exe_tables / C5_EXE_TABLES; missing: the test
  FAILS naming the setting, never skips), compared with the golden EXE file of both builds. Returns
  {build: [(name, at_symbol, reason)]} of the rows that do not match (empty lists = all OK). Patterns of 16 bytes or more
  that compare every byte are also checked to occur exactly once in the file's mapped image (as before 4964).
- va(build, name): a symbol's VA in a build (anchor build: the catalogue; others: the local table).

Nothing here prints a build-26577625 address; failures name symbols and offsets only.
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
if str(REPO / "tools" / "exebuild") not in sys.path:
    sys.path.insert(0, str(REPO / "tools" / "exebuild"))
if str(REPO) not in sys.path:
    sys.path.append(str(REPO))
import gen_table  # noqa: E402
import pe32  # noqa: E402
import table  # noqa: E402
from tools.harness import exe_build  # noqa: E402

BUILDS = ("4390913", "26577625")
ANCHOR = BUILDS[0]


def golden(build: str) -> Path:
    exe = exe_build.golden_install(build) / exe_build.EXE_NAME
    assert exe.is_file(), ("the golden copy of build %s is missing: %s (exe_build.golden_install, C5_GOLDEN_INSTALL_%s)"
                           % (build, exe, build))
    return exe


def values(build: str) -> dict:
    """name -> VA of every catalogue symbol in the build (the local table; FAILS naming exe_tables when it is missing)."""
    try:
        t = table.load(build, chash=table.catalogue_hash())   # this checkout's catalogue's copy when there is one
    except table.TableError as e:
        raise AssertionError("the local build table of %s is needed (setting exe_tables / C5_EXE_TABLES): %s" % (build, e))
    assert t.get("catalogue_sha256") == table.catalogue_hash(), (
        "the local table of build %s is stale (another catalogue): run tools/exebuild/gen_table.py for it again" % build)
    return {n: int(s["va"], 16) for n, s in t["symbols"].items() if s.get("va")}


def va(build: str, name: str) -> int:
    v = values(build).get(name)
    assert v, "%s is not carried to build %s" % (name, build)
    return v


def rows(rel: str):
    """[(file, description, at, template)] of a source file's X(...) table."""
    return gen_table.read_pattern_tables([rel])


def check_patterns(rel: str) -> dict:
    syms = table.symbols()
    names = {s.name for s in syms}
    by_anchor = {s.anchor: s.name for s in syms if s.anchor is not None}
    out = {}
    for build in BUILDS:
        pe = pe32.PE(str(golden(build)))
        vals = values(build)
        bad = [(name, at, reason) for _f, name, at, res, reason in
               gen_table.check_patterns(rows(rel), pe, vals, by_anchor, names) if res != "OK"]
        image = None
        for _f, name, at, tmpl in rows(rel):
            exp = gen_table.expected_bytes(gen_table.parse_template(tmpl),
                                           lambda o: vals.get(gen_table.operand_symbol(o, by_anchor, names)))
            if len(exp) >= 16 and None not in exp:
                if image is None:
                    image = b"".join(pe.read_rva(s["rva"], max(s["vsize"], s["raw_size"])) or b"" for s in pe.sections)
                n = image.count(bytes(exp))
                if n != 1:
                    bad.append((name, at, "occurs %d times in the image (want once)" % n))
        out[build] = bad
    return out
