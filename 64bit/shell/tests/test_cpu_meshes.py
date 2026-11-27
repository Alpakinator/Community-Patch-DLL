"""Ticket 4879 (epic 0561): the CPU-built meshes in the compositor (shell/composite/unit_filter.cpp, the geometry section of
shell/common/shell_model_tap.h) on the stand-in host unit_filter_host.exe with --models --cpu: two CPU-built cells (R32,
IMMUTABLE: C0 on the block programs with 32-byte vertices and one slot-1 instance, a wall's form; C1 on the material-3
programs with 20-byte vertices and VS cb0, the Great Wall's and a bridge's form). The host plays the server: it reads the
geometry section, compares every entry's bytes with what it uploaded ("C" lines: cells, held, check) and, with
--cpu-set held, publishes the two keys in a new identity set generation once all three buffers are entries.

Every refusal is a case with its outputs and exit code: never in the set, the section full (model_geom_test=full: in the
set but not held), oversize (model_geom_test=oversize:32), a copy that differs from its creation hash
(model_geom_test=changed: the refusal of a write after the copy), cities not wanted (no copy at all).
Ticket 0597: the section really full (model_geom_test=bytes:128): one logged refusal with nothing to evict, and the LRU
freeing a buffer the host stops drawing (--cpu-stop) for the refused one; model_geom_kb read, clamped, defaulted.
Design and results: research/05-graphics-composition/city-renderer.md section 13.
Run: `python -I -m pytest shell/tests/test_cpu_meshes.py` with C5_SHELL_BUILD and PYTEST_DISABLE_PLUGIN_AUTOLOAD=1.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import pytest  # noqa: E402,F401

from test_model_filter import CITIES, LANDMARKS, Mf, mf, mtable  # noqa: E402,F401  (fixtures re-used)
from test_unit_filter import parse  # noqa: E402


def cframes(m):
    out = []
    for ln in m.run.log_lines("host"):
        x = re.search(r"\] C (\d+) cells=(\w+) held=(\d+) geom_gen=(\d+) set_gen=(\d+) check=(.*)$", ln)
        if x:
            out.append(dict(f=int(x.group(1)), cells=x.group(2), held=int(x.group(3)), ggen=int(x.group(4)), sgen=int(x.group(5)),
                            check=x.group(6).strip()))
    return out


def gtotals(m):
    t = [ln for ln in m.lines() if "modelgeom totals:" in ln]
    return parse(t[-1]) if t else None


def records_ok(m):
    bad = [x for x in m.mframes() if x["seq"] and x["check"] != "ok"]
    assert not bad, bad[:3]


def test_cpu_cells_drop_once_held(mf):
    """Cities latched from frame 0: both CPU cells are copied into the geometry section (three buffers: the shared R32
    index buffer, the 32-B and the 20-B vertex buffers), the host finds their bytes equal, publishes their keys, and from
    then on both cells are dropped with complete records; before that they pass."""
    mf.go("--cpu", "--cpu-set", "held", "--frames", "16", "--word", "0:%d" % CITIES, "--want", "0x6")
    c = cframes(mf)
    assert len(c) == 16, c
    assert all(x["check"] == "ok" for x in c), c
    held_at = next(x["f"] for x in c if x["held"] == 3)
    assert held_at <= 6, c
    assert all(x["cells"] == "PP" for x in c if x["f"] <= held_at), c
    assert c[-1]["sgen"] == 2 and c[-1]["cells"] == "DD", c
    assert all(ch in "PD" for x in c for ch in x["cells"]), c
    records_ok(mf)
    t = mf.mtotals()
    assert t["cpu_seen"] > 0 and t["off"] == 0, t
    g = gtotals(mf)
    assert g and g["section"] == "made" and g["held"] == 3 and g["entries"] == 3 and g["wanted"] == 3, g
    assert g["oversize"] == g["changed"] == g["full"] == g["failed"] == g["notkeyable"] == 0, g
    assert any("model_filter: geometry section" in ln for ln in mf.lines())
    assert not [ln for ln in mf.lines() if "model_filter: LOUD" in ln]
    assert not [ln for ln in mf.lines() if "model_filter: REFUSED" in ln]   # ticket 0597
    assert g["section_b"] == 32768 * 1024 and g["full_logged"] == 0 and g["max_entries"] == 3, g   # ticket 0597
    # the ordinary model cells are unaffected
    assert mf.mframes()[-1]["imm"] == "DD" + "P" * 2 + "D" + "P" * 4


def test_cpu_keys_never_in_set_pass(mf):
    """A CPU-built draw whose key the server never publishes (it lacks the geometry or its textures): copied, never dropped."""
    mf.go("--cpu", "--cpu-set", "never", "--frames", "10", "--word", "0:%d" % CITIES, "--want", "0x6")
    c = cframes(mf)
    assert all(x["cells"] == "PP" for x in c), c
    assert c[-1]["held"] == 3 and c[-1]["sgen"] == 1, c
    t = mf.mtotals()
    assert t["cpu_seen"] > 0 and t["not_in_set"] > 0 and t["cpu_nogeom"] == 0, t
    records_ok(mf)


def test_cpu_section_full_passes(mf):
    """In the set but the section never has room (model_geom_test=full): the draws pass and count cpu_nogeom; nothing held."""
    mf.go("--cpu", "--cpu-set", "always", "--frames", "10", "--word", "0:%d" % CITIES, "--want", "0x6",
          env={"VPSHELL_MODEL_GEOM_TEST": "full"})
    c = cframes(mf)
    assert all(x["cells"] == "PP" and x["held"] == 0 for x in c), c
    t = mf.mtotals()
    assert t["cpu_nogeom"] > 0, t
    g = gtotals(mf)
    assert g["held"] == 0 and g["full"] >= 3 and g["entries"] == 0, g
    assert any("model_geom_test=full - TEST ONLY" in ln and "LOUD" in ln for ln in mf.lines())
    records_ok(mf)


def test_cpu_oversize_refused(mf):
    """A buffer over the bound (model_geom_test=oversize:32: both vertex buffers, 96 and 60 bytes) is refused; the index
    buffer (24 bytes) is held; neither cell is dropped although both keys are in the set."""
    mf.go("--cpu", "--cpu-set", "always", "--frames", "10", "--word", "0:%d" % CITIES, "--want", "0x6",
          env={"VPSHELL_MODEL_GEOM_TEST": "oversize:32"})
    c = cframes(mf)
    assert all(x["cells"] == "PP" for x in c), c
    assert c[-1]["held"] == 1, c
    g = gtotals(mf)
    assert g["oversize"] == 2 and g["held"] == 1, g
    assert mf.mtotals()["cpu_nogeom"] > 0
    records_ok(mf)


def test_cpu_copy_changed_refused(mf):
    """Every copy reads as differing from the hash taken at creation (model_geom_test=changed: a write after the hash):
    refused, never entered, never dropped."""
    mf.go("--cpu", "--cpu-set", "always", "--frames", "10", "--word", "0:%d" % CITIES, "--want", "0x6",
          env={"VPSHELL_MODEL_GEOM_TEST": "changed"})
    c = cframes(mf)
    assert all(x["cells"] == "PP" and x["held"] == 0 for x in c), c
    g = gtotals(mf)
    assert g["changed"] == 3 and g["held"] == 0, g
    records_ok(mf)


def cstop(m):
    for ln in m.run.log_lines("host"):
        x = re.search(r"\] Cstop (\d+) cell=(\d) mask=(\d+)", ln)
        if x:
            return dict(f=int(x.group(1)), cell=int(x.group(2)), mask=int(x.group(3)))
    return None


def test_cpu_section_full_logged_nothing_to_evict(mf):
    """Ticket 0597: a section of 128 data bytes (model_geom_test=bytes:128) holds the 24-B index buffer (32 aligned) and one
    of the two vertex buffers (96 or 60 B); the other does not fit and, both cells being drawn every frame, nothing is
    unnamed long enough to evict (the live case: the EXE draws every CPU-built mesh even off screen). The refusal is one
    logged line (not silent), the cell whose buffers are held is dropped (its key is in the set), the other passes."""
    mf.go("--cpu", "--cpu-set", "always", "--frames", "14", "--word", "0:%d" % CITIES, "--want", "0x6",
          env={"VPSHELL_MODEL_GEOM_TEST": "bytes:128"})
    c = cframes(mf)
    assert c and all(x["check"] == "ok" for x in c), c
    assert c[-1]["held"] == 2, c
    g = gtotals(mf)
    assert g["section_b"] == 128 and g["entries"] == 2 and g["held"] == 2, g
    assert g["full"] >= 1 and g["full_logged"] == 1 and g["evicted_room"] == 0 and g["evicted"] == 0, g
    assert g["used_b"] in (32 + 96, 32 + 64) and g["max_entries"] == 2 and g["max_used_b"] == g["used_b"], g
    refused = [ln for ln in mf.lines() if "model_filter: REFUSED full:" in ln]
    assert len(refused) == 1 and "the EXE draws this mesh (not lost)" in refused[0], refused
    assert "model_geom_test=bytes:128 (TEST ONLY)" in refused[0], refused
    assert any("model_geom_test=bytes:128 - TEST ONLY" in ln and "LOUD" in ln for ln in mf.lines())
    # exactly one cell has both buffers in the section: that one is dropped, the other passes (cpu_nogeom)
    assert sorted(c[-1]["cells"]) == ["D", "P"], c
    assert mf.mtotals()["cpu_nogeom"] > 0
    records_ok(mf)


def test_cpu_section_full_lru_evicts_unnamed(mf):
    """Ticket 0597: as above, then from frame 8 the host stops drawing the cell whose vertex buffer is held (its buffer goes
    unnamed). When the refused buffer is tried again (600 Presents after its refusal) the LRU frees the unnamed buffer (not
    named for more than 120 Presents) and the refused one is held: the other cell is dropped from then on; one refusal
    line, one 'is held now' line, evicted_room 1."""
    mf.go("--cpu", "--cpu-set", "always", "--frames", "660", "--word", "0:%d" % CITIES, "--want", "0x6", "--cpu-stop", "8",
          env={"VPSHELL_MODEL_GEOM_TEST": "bytes:128"}, seconds=400)
    c = cframes(mf)
    assert c and all(x["check"] == "ok" for x in c), [x for x in c if x["check"] != "ok"][:3]
    s = cstop(mf)
    assert s and s["f"] == 8 and s["mask"] in (3, 5), s
    other = 1 - s["cell"]
    assert c[-1]["cells"][s["cell"]] == "S" and c[-1]["cells"][other] == "D", c[-1]
    # before the retry the other cell passed (its vertex buffer refused)
    assert all(x["cells"][other] == "P" for x in c if 9 <= x["f"] <= 500), [x for x in c if 9 <= x["f"] <= 500 and x["cells"][other] != "P"][:3]
    g = gtotals(mf)
    assert g["evicted_room"] == 1 and g["evicted"] == 1 and g["full"] == 1 and g["full_logged"] == 1, g
    assert g["held"] == 2 and g["entries"] == 2, g
    assert c[-1]["held"] == 2, c[-1]
    assert len([ln for ln in mf.lines() if "model_filter: REFUSED full:" in ln]) == 1
    held_now = [ln for ln in mf.lines() if "refused full 1 time(s) is held now" in ln]
    assert len(held_now) == 1, held_now
    assert g["compactions"] == 0, g   # one hole that fits after the eviction
    records_ok(mf)


def cstop_ofs(m):
    for ln in m.run.log_lines("host"):
        x = re.search(r"\] Cstop (\d+) cell=(\d) mask=(\d+) ofs=(-?\d+)/(-?\d+)/(-?\d+)", ln)
        if x:
            return [int(x.group(i)) for i in range(1, 7)]
    return None


def test_cpu_section_fragmented_compacts(mf):
    """Ticket 0597, the live Tiny run's case (2026-11-20: 252,544 B free in two holes, a 226,380-B buffer refused for good):
    a 320-B section; cell 0's vertex buffer (112 B, --cpu-frag: copied before the index buffer) at 0, the index buffer at 112;
    cell 1 from frame 10 (--cpu-late) with a 200-B vertex buffer (--cpu-pad 140, 208 aligned) that does not fit the 176-B
    tail: refused, logged. Cell 0 stops at frame 20; at the retry the LRU frees its vertex buffer (a 112-B hole at 0 and the
    176-B tail: 288 B free, no hole of 208), the data is compacted (the index buffer moved to 0) and the buffer is held: cell
    1 dropped from then on, the host's bytes check of every entry holds after the move."""
    mf.go("--cpu", "--cpu-set", "always", "--frames", "700", "--word", "0:%d" % CITIES, "--want", "0x6", "--cpu-frag",
          "--cpu-late", "10", "--cpu-pad", "140", "--cpu-stop", "20", env={"VPSHELL_MODEL_GEOM_TEST": "bytes:320"}, seconds=400)
    assert any("cpu-frag: seed" in ln for ln in mf.run.log_lines("host"))
    c = cframes(mf)
    assert c and all(x["check"] == "ok" for x in c), [x for x in c if x["check"] != "ok"][:3]
    s = cstop_ofs(mf)
    assert s and s[0] == 20 and s[1] == 0 and s[2] == 3 and s[3:] == [112, 0, -1], s
    assert all(x["cells"][1] == "L" for x in c if x["f"] < 10), c[:12]
    assert all(x["cells"][1] == "P" for x in c if 12 <= x["f"] <= 500), [x for x in c if 12 <= x["f"] <= 500 and x["cells"][1] != "P"][:3]
    assert c[-1]["cells"] == "SD" and c[-1]["held"] == 2, c[-1]
    g = gtotals(mf)
    assert g["compactions"] == 1 and g["evicted_room"] == 1 and g["full"] == 1 and g["full_logged"] == 1, g
    assert g["entries"] == 2 and g["used_b"] == 32 + 208 and g["map_fail"] == 0, g
    comp = [ln for ln in mf.lines() if "geometry section compacted for a CPU-built buffer of 200 B: 288 B were free" in ln]
    assert len(comp) == 1 and "24 B moved" in comp[0], comp
    assert len([ln for ln in mf.lines() if "refused full 1 time(s) is held now" in ln]) == 1
    records_ok(mf)


@pytest.mark.parametrize("value,section_b,note", [
    ("1024", 1024 * 1024, "model_geom_kb=1024"),
    ("5", 1024 * 1024, "model_geom_kb=5 out of 1024..131072: clamped"),
    ("999999", 131072 * 1024, "model_geom_kb=999999 out of 1024..131072: clamped"),
    ("abc", 32768 * 1024, "model_geom_kb=abc is not a number: the default"),
])
def test_cpu_model_geom_kb(mf, value, section_b, note):
    """Ticket 0597: model_geom_kb sets the section's data size (KB, 1,024..131,072, default 32,768); out of range is
    clamped, not a number is the default; both are noted in the section's line. The cells are held and dropped as usual."""
    mf.go("--cpu", "--cpu-set", "held", "--frames", "10", "--word", "0:%d" % CITIES, "--want", "0x6",
          env={"VPSHELL_MODEL_GEOM_KB": value})
    g = gtotals(mf)
    assert g["section_b"] == section_b and g["held"] == 3 and g["full"] == 0, g
    sec = [ln for ln in mf.lines() if "model_filter: geometry section Local" in ln]
    assert len(sec) == 1 and note in sec[0] and ("%u B;" % section_b) in sec[0], sec
    assert not [ln for ln in mf.lines() if "model_filter: REFUSED" in ln or "model_filter: LOUD" in ln]
    records_ok(mf)


def test_cpu_not_copied_without_cities(mf):
    """Only landmarks wanted and latched: the CPU-built cells are city art, so nothing is copied (no geometry section) and
    they pass."""
    mf.go("--cpu", "--cpu-set", "held", "--frames", "8", "--word", "0:%d" % LANDMARKS, "--want", "0x4")
    c = cframes(mf)
    assert all(x["cells"] == "PP" and x["held"] == 0 for x in c), c
    assert mf.mtotals()["cpu_seen"] == 0
    assert gtotals(mf) is None
    assert not any("model_filter: geometry section" in ln for ln in mf.lines())
