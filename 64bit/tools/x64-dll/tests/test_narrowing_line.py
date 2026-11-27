"""Ticket 4810: the narrowing gate of tools/x64-dll/build.py keys every -Wshorten-64-to-32 site by the text of the
PHYSICAL source line, also when the file carries '#line N' directives (the speed packages of epic 0504 put one after
each speed block so __LINE__ does not move).

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest tools/x64-dll/tests/test_narrowing_line.py -q -p no:cacheprovider

The compiler tests run the real clang-cl with build.py's own flags (cl_args(..., shorten=True)) on small files and are
skipped when LLVM is not installed; the rest are offline.
"""
from __future__ import annotations

import importlib.util
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
X64 = HERE.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


b = _load("x64build_4810", X64 / "build.py")
needs_clang = pytest.mark.skipif(not b.CLANG_CL.exists(), reason="LLVM clang-cl not installed")

# The "pin" file: two narrowing sites (lines 4 and 8). The other files are this one with something inserted.
PIN = """#include <stddef.h>
int f(size_t a, size_t b)
{
    int x = a;
    int s = 0;
    for (int i = 0; i < 3; i++)
        s += i;
    int y = b;
    return x + y + s;
}
"""

# A speed block after line 4 and '#line 5' after it: N = the number of the next line in the pin's file.
BLOCK = """#include <stddef.h>
int f(size_t a, size_t b)
{
    int x = a;
#if defined(VP64_SPEED_DEMO)
    int z = b;
    x += z;
#endif
#line 5
    int s = 0;
    for (int i = 0; i < 3; i++)
        s += i;
    int y = b;
    return x + y + s;
}
"""

# A directive naming another file before the second site.
OTHER = PIN.replace("    int y = b;\n", '#line 40 "gen/other.cpp"\n    int y = b;\n#line 10\n')

# A directive inside '#if 0': the preprocessor skips it, so nothing moves.
IFZERO = PIN.replace("    int s = 0;\n", "#if 0\n#line 900\n#endif\n    int s = 0;\n")


def keys_of(tmp: Path, name: str, text: str, defines=(), extra_drop=()) -> dict:
    """Compile <tmp>/<name>/G/u.cpp with build.py's release-front-end flags (config fast, --shorten) and return
    narrowing_keys of its sites, keyed relative to <tmp>/<name>."""
    src = tmp / name
    (src / "G").mkdir(parents=True)
    (src / "G" / "u.cpp").write_text(text, encoding="utf-8", newline="\n")
    args = [x for x in b.cl_args(src, "fast", True) if x not in extra_drop]
    args = [x for x in args if not x.startswith("/I")]
    cmd = [str(b.CLANG_CL)] + args + [f"/D{d}" for d in defines] + ["G/u.cpp", f"/Fo{tmp / (name + '.obj')}"]
    cp = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd=str(src))
    assert cp.returncode == 0, cp.stdout + cp.stderr
    out = tmp / (name + "-out")
    out.mkdir()
    _, nerr, sites = b.summarize_diags({"G/u.cpp": cp.stdout + cp.stderr}, out, lambda *a, **k: None)
    assert nerr == 0
    return b.narrowing_keys(sites, src.resolve())


def codes(keys: dict) -> dict:
    return {(k[0], k[3]): sorted(v) for k, v in keys.items()}


def strip_lines(keys: dict) -> dict:
    return {k: len(v) for k, v in keys.items()}


def test_cl_args_carry_physical_locations_only_with_shorten():
    src = Path(r"D:\some\src")
    on, off = b.cl_args(src, "release", True), b.cl_args(src, "release", False)
    assert on[on.index(b.SHORTEN_FLAG) + 1:on.index(b.SHORTEN_FLAG) + 3] == b.PHYSICAL_LOC_ARGS
    assert not set(b.PHYSICAL_LOC_ARGS) & set(off)
    assert [x for x in on if x not in [b.SHORTEN_FLAG] + b.PHYSICAL_LOC_ARGS] == off


@needs_clang
def test_no_directive_keys_unchanged(tmp_path):
    """A file without '#line': the keys with the new flag equal the keys of the old command line."""
    new = keys_of(tmp_path, "new", PIN)
    old = keys_of(tmp_path, "old", PIN, extra_drop=b.PHYSICAL_LOC_ARGS)
    assert new == old
    assert codes(new) == {("G/u.cpp", "int x = a;"): [4], ("G/u.cpp", "int y = b;"): [8]}


@needs_clang
def test_speed_block_with_line_directive(tmp_path):
    pin = keys_of(tmp_path, "pin", PIN)
    off = keys_of(tmp_path, "off", BLOCK)
    # define off: the same keys as the pin's file (lines are informative and physical: 13 in the branch, 8 in the pin)
    assert strip_lines(off) == strip_lines(pin)
    assert codes(off)[("G/u.cpp", "int y = b;")] == [13]
    # define on: exactly the block's own site is added
    on = keys_of(tmp_path, "on", BLOCK, defines=["VP64_SPEED_DEMO"])
    added = {k: n for k, n in strip_lines(on).items() if strip_lines(pin).get(k, 0) != n}
    assert {(k[0], k[3]): n for k, n in added.items()} == {("G/u.cpp", "int z = b;"): 1}
    # the old command line (presumed locations) reproduces 4803's failure: it reads line 8 of the branch for 'y'
    old = keys_of(tmp_path, "old", BLOCK, extra_drop=b.PHYSICAL_LOC_ARGS)
    assert strip_lines(old) != strip_lines(pin)
    assert ("G/u.cpp", "#endif") in codes(old)


@needs_clang
def test_directive_naming_another_file(tmp_path):
    """'#line 40 "gen/other.cpp"': clang would name gen/other.cpp; with the flag the site stays in G/u.cpp."""
    pin = keys_of(tmp_path, "pin", PIN)
    other = keys_of(tmp_path, "other", OTHER)
    assert strip_lines(other) == strip_lines(pin)
    assert codes(other)[("G/u.cpp", "int y = b;")] == [9]
    with pytest.raises(b.NarrowingSiteError):  # the old command line names a file that does not exist
        keys_of(tmp_path, "old", OTHER, extra_drop=b.PHYSICAL_LOC_ARGS)


@needs_clang
def test_directive_inside_if_zero(tmp_path):
    """A '#line' inside '#if 0' is skipped by the preprocessor: keys equal the pin's under either command line.
    (Any '#line' inside a conditional is the compiler's to apply or skip; the gate reads physical lines either way.)"""
    pin = keys_of(tmp_path, "pin", PIN)
    assert strip_lines(keys_of(tmp_path, "new", IFZERO)) == strip_lines(pin)
    assert strip_lines(keys_of(tmp_path, "old", IFZERO, extra_drop=b.PHYSICAL_LOC_ARGS)) == strip_lines(pin)


@needs_clang
def test_line_probe_passes_and_catches_a_remapping_compiler(tmp_path, monkeypatch):
    assert b.check_physical_lines(None, tmp_path / "a") is None
    monkeypatch.setattr(b, "PHYSICAL_LOC_ARGS", [])  # stands in for a clang that ignores the flag
    err = b.check_physical_lines(None, tmp_path / "b")
    assert err and "vp-line-probe-logical.c" in err


def test_unreadable_site_fails_loud(tmp_path):
    (tmp_path / "a.cpp").write_text("int x = a;\n", encoding="utf-8")
    msg = "implicit conversion loses integer precision"
    keys = b.narrowing_keys({(str(tmp_path / "a.cpp"), 1, 9, msg)}, tmp_path.resolve())
    assert list(keys) == [("a.cpp", 9, msg, "int x = a;")]
    with pytest.raises(b.NarrowingSiteError, match="line past the end"):
        b.narrowing_keys({(str(tmp_path / "a.cpp"), 2, 9, msg)}, tmp_path.resolve())
    with pytest.raises(b.NarrowingSiteError, match="file not readable"):
        b.narrowing_keys({(str(tmp_path / "missing.cpp"), 1, 9, msg)}, tmp_path.resolve())
