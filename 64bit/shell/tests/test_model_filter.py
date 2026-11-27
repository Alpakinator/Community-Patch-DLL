"""Ticket 4874 (epic 0561): the compositor's model filter and tap (shell/composite/unit_filter.cpp, the units' recogniser
generalised), on the units' stand-in host unit_filter_host.exe with --models (shell/tests/unit_filter_host.cpp: nine model
cells on the immediate and nine on a deferred context per frame; the host also plays the server: the identity set, the
control block's wantBits and setGeneration, and the reader of every tap frame, which checks the records against what it
drew and acks). Design and results: research/05-graphics-composition/city-renderer.md section 10.

Cells (per context): 0 block (cities), 1 single (cities, DrawIndexed, cb0 per draw), 2 instanced (landmarks), 3 skinned
(landmarks), 4 skinned (cities, two instances), 5 instanced not in the set, 6 instanced with another t0 (same buffers and
range: the euro/medit case), 7 instanced without a depth view (the mask's form), 8 the mask pass (pass 1).

Every negative test checks the outputs (cells, records, filterState2, counters, log lines) and the host's exit code.
Run: `python -I -m pytest shell/tests/test_model_filter.py` with C5_SHELL_BUILD set to a build of shell/build.cmd (plain or
asan) and PYTEST_DISABLE_PLUGIN_AUTOLOAD=1. A missing binary fails (no skip).
"""
import hashlib
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import pytest  # noqa: E402

import shellrun  # noqa: E402
from test_unit_filter import KEYS, host_exe, parse  # noqa: E402

CITIES, LANDMARKS = 0x2, 0x4
CAN = 0x1
R_PATH2_REFUSED, R_PATH2_NOT_RUN, R_EXCEPTION, R_NO_DROP, R_MODE_OFF, R_TABLE_REFUSED, R_MARK_FULL, R_NO_HOOKS = range(1, 9)
R_NO_SET, R_TAP_FAILED = 9, 10
CELL_BITS = [CITIES, CITIES, LANDMARKS, LANDMARKS, CITIES, 0, 0, 0, 0]   # the bit under which each cell may be dropped
ALL_P = "P" * 9
UNITS_DROPPED = "D" * 6 + "P" * 6


def cannot(reason):
    return 0x2 | (reason << 8)


def fs2(cities, landmarks):
    return cities | (landmarks << 16)


def expect(bits):
    """The cell string when `bits` are latched (and the record is complete)."""
    return "".join("D" if b & bits else "P" for b in CELL_BITS)


@pytest.fixture(scope="session")
def mtable(tmp_path_factory, hidden_desktop):
    """The test table with the model programs: 0..11 the units' shaders, 12..21 the host's model shaders 14..23."""
    d = tmp_path_factory.mktemp("mfdump")
    p = shellrun.launch([str(host_exe()), "--dump", str(d), "--log", str(d / "dump.log")], hidden_desktop)
    try:
        assert p.wait(60) == 0, shellrun.tail(d / "dump.log", 10)
    finally:
        p.kill()
        p.close()
    dig = [hashlib.sha256((d / ("shader%02d.bin" % i)).read_bytes()).hexdigest() for i in range(24)]
    assert len(set(dig)) == 24, "the host's 24 shaders must be distinct"
    lines = ["%d %s" % (i, dig[i]) for i in range(12)] + ["%d %s" % (12 + j, dig[14 + j]) for j in range(10)]
    t = d / "mtable.txt"
    t.write_text("# test table (4874)\n" + "\n".join(lines) + "\n", encoding="utf-8")
    partial = d / "partial.txt"   # the model part incomplete: refused whole
    partial.write_text("\n".join(lines[:-1]) + "\n", encoding="utf-8")
    return t, partial


class Mf:
    def __init__(self, run, table):
        self.run = run
        self.table = table
        self.code = None
        run.set_ini(**KEYS)

    def go(self, *args, env=None, seconds=90, table=None, expect_code=0, hold=False):
        # ticket 0788: the tests below pin the frame a cell's drops start in, so they run with the one-frame hold of a newly
        # droppable key off (C5_SHELL_MODEL_HOLD=0, the diagnostic switch); the hold's own tests (hold=True) run the product's default
        e = {"VPSHELL_LOG": str(self.run.composite_log), "VPSHELL_SECTION": self.run.section,
             "VPSHELL_UNIT_FILTER_TABLE": str(table or self.table)}
        if not hold:
            e["C5_SHELL_MODEL_HOLD"] = "0"
        e.update(env or {})
        cmd = [str(host_exe()), "--dll", str(self.run.dll), "--log", str(self.run.host_log), "--section", self.run.section,
               "--models", *args]
        p = shellrun.launch(cmd, self.run.desktop, env=e)
        self.code = p.wait(seconds)
        if self.code is None:
            p.kill()
        p.close()
        assert self.code == expect_code, (self.code, shellrun.tail(self.run.host_log, 12), shellrun.tail(self.run.composite_log, 12))
        return self

    def mframes(self):
        out = []
        for ln in self.run.log_lines("host"):
            m = re.search(r"\] M (\d+) imm=(\w+) def=(\w+) fs2=0x([0-9A-F]+) dc=(\d+) dl=(\d+) pc=(\d+) pl=(\d+) seq=(\d+) nd=(\d+) "
                          r"ndrop=(\d+) check=(.*)$", ln)
            if m:
                out.append(dict(f=int(m.group(1)), imm=m.group(2), def_=m.group(3), fs2=int(m.group(4), 16), dc=int(m.group(5)),
                                dl=int(m.group(6)), pc=int(m.group(7)), pl=int(m.group(8)), seq=int(m.group(9)), nd=int(m.group(10)),
                                ndrop=int(m.group(11)), check=m.group(12).strip()))
        return out

    def uframes(self):
        out = []
        for ln in self.run.log_lines("host"):
            m = re.search(r"\] F (\d+) imm=(\w+) def=(\w+) latched=(\d+) .* fstate=0x([0-9A-F]+)", ln)
            if m:
                out.append(dict(f=int(m.group(1)), imm=m.group(2), def_=m.group(3), latched=int(m.group(4)), fstate=int(m.group(5), 16)))
        return out

    def lines(self):
        return self.run.log_lines("composite")

    def mtotals(self):
        t = [ln for ln in self.lines() if "modelfilter totals:" in ln]
        assert t, "no 'modelfilter totals:' line; composite log tail: %s" % shellrun.tail(self.run.composite_log, 12)
        return parse(t[-1])


@pytest.fixture
def mf(tmp_path, hidden_desktop, built, request, mtable):
    r = shellrun.Run(tmp_path / "run", hidden_desktop, "mf" + request.node.name[-18:].replace("[", "_").replace("]", ""))
    m = Mf(r, mtable[0])
    yield m
    r.stop()


def warmup_ok(cells, bits):
    """Before every copy covers its draw: a latched cell may still pass, an unlatched one never drops."""
    return all(c == "P" or (c == "D" and CELL_BITS[i] & bits) for i, c in enumerate(cells))


def check_drops(fr, bits, first, imm=True, deferred=True):
    """From frame `first` on, the cells latched under `bits` are dropped (and every record checked). Before it the copies
    warm up (the skinned palette's high-water mark is known one frame after the slot-1 ids: its cells drop a frame later):
    a latched cell may pass, nothing else drops, and every frame's records still check."""
    assert fr, "no M lines"
    for x in fr:
        for on, got in ((imm, x["imm"]), (deferred, x["def_"])):
            if not on:
                continue
            if x["f"] >= first:
                assert got == expect(bits), (x, expect(bits))
            else:
                assert warmup_ok(got, bits), x
        if x["seq"] and bits:
            assert x["check"] == "ok", x


# ---------------------------------------------------------------------------------------------------------------------
# The drop rule per class and bit, the tap's records (path 1: the host's shaders created after the hooks)
# ---------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("word", [CITIES, LANDMARKS, CITIES | LANDMARKS])
def test_each_bit_drops_its_classes(mf, word):
    """Latched from frame 0's Present: the tap starts in frame 1 (the draws mark the buffers it copies), the copies exist
    from frame 2's Unmaps, so the drops start in frame 2, on both contexts; every frame's records equal what was drawn."""
    mf.go("--frames", "8", "--word", "0:%d" % word)
    fr = mf.mframes()
    assert [x["f"] for x in fr] == list(range(8))
    check_drops(fr, word, 3)
    assert all(x["fs2"] == fs2(CAN, CAN) for x in fr[1:]), [(x["f"], hex(x["fs2"])) for x in fr]
    last = fr[-1]
    # the mode block's counts equal the dropped cells read back, per bit
    n_c = sum(1 for x in fr for cells in (x["imm"], x["def_"]) for i, ch in enumerate(cells) if ch == "D" and CELL_BITS[i] == CITIES)
    n_l = sum(1 for x in fr for cells in (x["imm"], x["def_"]) for i, ch in enumerate(cells) if ch == "D" and CELL_BITS[i] == LANDMARKS)
    assert (last["dc"], last["dl"]) == (n_c, n_l), last
    # passed while latched: cells 5, 6 (not in the set) when landmarks are latched; cell 7 has no depth view; frame 1's
    # draws had no copy yet
    t = mf.mtotals()
    assert t["off"] == 0 and t["tapped"] > 0 and t["mismatch"] == 0 and t["place_miss"] == 0, t
    if word & LANDMARKS:
        assert t["not_in_set"] > 0 and t["no_dsv"] > 0, t
    assert not [ln for ln in mf.lines() if "model_filter: LOUD" in ln], [ln for ln in mf.lines() if "LOUD" in ln]
    # the units' cells are untouched by the model bits
    assert all(x["imm"] == "P" * 12 and x["def_"] == "P" * 12 for x in mf.uframes())


def test_models_and_units_together(mf):
    mf.go("--frames", "6", "--word", "0:7")
    fr = mf.mframes()
    check_drops(fr, CITIES | LANDMARKS, 3)
    uf = mf.uframes()
    assert all(x["imm"] == UNITS_DROPPED and x["def_"] == UNITS_DROPPED for x in uf if x["f"] >= 1), uf
    assert all(x["fstate"] == 0x1 for x in uf[1:]), uf


def test_word_cleared_stops_the_drops(mf):
    mf.go("--frames", "9", "--word", "0:6", "--word", "5:0:mid")
    fr = mf.mframes()
    for x in fr:
        if x["f"] == 2:
            assert warmup_ok(x["imm"], 6) and warmup_ok(x["def_"], 6), x
            continue
        want = expect(6) if 3 <= x["f"] <= 5 else ALL_P
        assert x["imm"] == want and x["def_"] == want, x


def test_deferred_list_recorded_before_the_maps(mf):
    """The deferred list is recorded before the immediate context's Maps of the frame: its records read the immediate
    copies at its ExecuteCommandList, so they hold this frame's contents (the reader checks them)."""
    mf.go("--frames", "8", "--word", "0:6", "--defer-early")
    fr = mf.mframes()
    # frame 1: the deferred draws, recorded first, mark the buffers, so the immediate Maps of frame 1 are copied and the
    # immediate cells drop at once; the deferred ones find a copy from frame 2 on
    check_drops(fr, 6, 2, deferred=False)
    check_drops(fr, 6, 3, imm=False)


def test_want_bits_tap_without_drops(mf):
    """wantBits without the mode bit: the server's classes are tapped (passed records), nothing is dropped."""
    mf.go("--frames", "6", "--want", "6")
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P for x in fr), fr
    assert any(x["nd"] > 0 for x in fr) and all(x["ndrop"] == 0 and x["check"] == "ok" for x in fr if x["seq"]), fr


def test_tap_passed_test_key(mf):
    mf.run.set_ini(model_tap_passed="1")
    mf.go("--frames", "5")
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P for x in fr) and any(x["nd"] >= 10 for x in fr), fr
    assert any("model_tap_passed=1 - TEST ONLY" in ln for ln in mf.lines())


def test_budget_passes_what_does_not_fit(mf):
    """A slot of 256 KB (budget half of it) and a block cell of 4,000 instances (256,000 bytes): the block cell passes
    (budget), every other cell is still dropped with a complete record."""
    mf.run.set_ini(model_tap_slot_kb="256")
    mf.go("--frames", "6", "--word", "0:6", "--minst", "4000")
    fr = mf.mframes()
    want = "P" + expect(6)[1:]
    assert all(x["imm"] == want and x["def_"] == want and x["check"] == "ok" for x in fr if x["f"] >= 3), fr
    t = mf.mtotals()
    assert t["budget"] > 0 and t["budget_fail"] > 0, t


# ---------------------------------------------------------------------------------------------------------------------
# The identity set: refusals, each with nothing dropped and filterState2 "no identity set"
# ---------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("kind,name", [("none", None), ("torn", "torn"), ("badhash", "hash"), ("badmagic", "magic"),
                                       ("pidother", "another server"), ("badversion", "version"), ("bounds", "bounds"),
                                       ("gen", "generation")])
def test_identity_set_refused(mf, kind, name):
    mf.go("--frames", "6", "--word", "0:6", "--mset", kind)
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P and x["ndrop"] == 0 for x in fr), fr
    assert all(x["fs2"] == fs2(cannot(R_NO_SET), cannot(R_NO_SET)) for x in fr[1:]), [hex(x["fs2"]) for x in fr]
    loud = [ln for ln in mf.lines() if "model_filter: LOUD" in ln]
    assert len(loud) == 2 and all("no identity set of this server" in ln for ln in loud), loud
    if name:
        assert any(re.search(r"model_filter: identity set generation 1 of server \d+: %s" % re.escape(name), ln) for ln in mf.lines()), \
            [ln for ln in mf.lines() if "identity set" in ln]
    t = mf.mtotals()
    assert t["set_keys"] == 0 and t["dropped_cities"] == 0 and t["dropped_landmarks"] == 0, t


def test_set_all_test_key(mf):
    """model_filter_set=all (TEST ONLY, for the live P5 check without a server): every fully hashed key counts as in the
    set under its class's bit (skinned: landmarks); only the depth-view and the pass rules still pass draws."""
    mf.go("--frames", "6", "--word", "0:6", "--mset", "none", env={"VPSHELL_MODEL_FILTER_SET": "all"})
    fr = mf.mframes()
    assert all(x["imm"] == "DDDDDDDPP" and x["def_"] == "DDDDDDDPP" for x in fr if x["f"] >= 3), fr
    assert all(x["fs2"] == fs2(CAN, CAN) for x in fr[1:]), [hex(x["fs2"]) for x in fr]
    assert any("model_filter_set=all - TEST ONLY" in ln and "LOUD" in ln for ln in mf.lines())


def test_read_back_of_parts_created_before_the_hooks(mf):
    """--early: the meshes and textures exist before the composite is loaded (not in its creation table). The first draws
    pass (uh_*_miss), each part is read back once (staging copy, a later Present's map), hashed as at creation, and the
    draws drop from then on with complete records."""
    mf.go("--frames", "10", "--word", "0:6", "--early")
    fr = mf.mframes()
    assert fr[1]["imm"] == ALL_P, fr[1]
    assert all(x["imm"] == expect(6) and x["def_"] == expect(6) and x["check"] == "ok" for x in fr if x["f"] >= 6), fr
    t = mf.mtotals()
    assert t["uh_ib_miss"] > 0 and t["uh_vb_miss"] > 0 and t["uh_tex_miss"] > 0, t
    # six index buffers, six vertex buffers, t0 (two), t1, t2-t4: 18 read back, none failed
    assert t["rb_hashed"] == 18 and t["rb_failed"] == 0 and t["rb_pending"] == 0, t


def test_a_later_write_clears_the_identity(mf):
    """UpdateSubresource into t0 after its hash: its identity is cleared (counted, never read back again), so every cell
    that binds it passes from that frame on; cell 6 (the other t0, not in the set) passes anyway."""
    mf.go("--frames", "9", "--word", "0:6", "--write-at", "5")
    fr = mf.mframes()
    assert all(x["imm"] == expect(6) for x in fr if 3 <= x["f"] <= 4), fr
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P for x in fr if x["f"] >= 5), fr
    t = mf.mtotals()
    assert t["write_cleared"] >= 1 and t["uh_tex_noid"] > 0 and t["rb_new"] == 0, t


def test_identity_set_late(mf):
    """The set published at frame 4: no drops before (filterState2 no set while latched), drops after."""
    mf.go("--frames", "9", "--word", "0:6", "--set-at", "4")
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P for x in fr if x["f"] <= 4), fr
    assert all(x["imm"] == expect(6) and x["check"] == "ok" for x in fr if x["f"] >= 6), fr
    assert fr[2]["fs2"] == fs2(cannot(R_NO_SET), cannot(R_NO_SET)) and fr[-1]["fs2"] == fs2(CAN, CAN), [hex(x["fs2"]) for x in fr]


# ---------------------------------------------------------------------------------------------------------------------
# Keys, kill switch, faults
# ---------------------------------------------------------------------------------------------------------------------
def test_model_filter_off(mf):
    mf.run.set_ini(model_filter="off")
    mf.go("--frames", "5", "--word", "0:6")
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P for x in fr), fr
    assert all(x["fs2"] == fs2(cannot(R_MODE_OFF), cannot(R_MODE_OFF)) for x in fr), [hex(x["fs2"]) for x in fr]
    assert any("model_filter=off - TEST ONLY" in ln for ln in mf.lines())
    assert not [ln for ln in mf.lines() if "model_filter: LOUD" in ln]


def test_model_filter_on(mf):
    mf.run.set_ini(model_filter="on")
    mf.go("--frames", "6")
    fr = mf.mframes()
    check_drops(fr, 6, 2)   # the bits are taken as set from the host's first Present: the tap runs in frame 0
    assert any("model_filter=on - TEST ONLY" in ln for ln in mf.lines())


@pytest.mark.parametrize("key,value,text", [("model_filter", "sometimes", "CONFIG ERROR model_filter='sometimes'"),
                                            ("model_tap_slot_kb", "100", "CONFIG ERROR model_tap_slot_kb='100'"),
                                            ("model_tap_slot_kb", "300", "CONFIG ERROR model_tap_slot_kb='300'"),
                                            ("model_map_cache", "2", "CONFIG ERROR model_map_cache='2'")])
def test_bad_key_values(mf, key, value, text):
    mf.run.set_ini(**{key: value})
    mf.go("--frames", "5", "--word", "0:6")
    assert any(text in ln for ln in mf.lines()), [ln for ln in mf.lines() if "CONFIG" in ln]
    check_drops(mf.mframes(), 6, 3)   # auto and the default slot


def test_kill_switch_hook_models(mf):
    mf.run.set_ini(hook_models="0")
    mf.go("--frames", "6", "--word", "0:6")
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P for x in fr), fr
    assert all(x["fs2"] == fs2(cannot(R_NO_HOOKS), cannot(R_NO_HOOKS)) for x in fr[3:]), [hex(x["fs2"]) for x in fr]
    assert any(re.search(r"\bhooks: .*\bmodels=0\b", ln) for ln in mf.lines())
    t = mf.mtotals()
    assert t["buf_init"] == 0 and t["tex_init"] == 0, t


@pytest.mark.parametrize("where", ["draw", "map", "create", "present", "execute"])
def test_fault_in_a_model_detour(mf, where):
    """One access violation in a model path: LOUD once, both filters off (every draw passes), the composite goes on."""
    mf.go("--frames", "7", "--word", "0:7", env={"VPSHELL_MODEL_FILTER_FAULT": where})
    loud = [ln for ln in mf.lines() if "unit_filter: LOUD: EXCEPTION" in ln]
    assert len(loud) == 1, [ln for ln in mf.lines() if "LOUD" in ln]
    fr = mf.mframes()
    assert fr[-1]["imm"] == ALL_P and fr[-1]["def_"] == ALL_P, fr
    assert fr[-1]["fs2"] == fs2(cannot(R_EXCEPTION), cannot(R_EXCEPTION)), hex(fr[-1]["fs2"])
    assert mf.uframes()[-1]["imm"] == "P" * 12 and mf.uframes()[-1]["fstate"] == cannot(R_EXCEPTION)
    rows = shellrun.stats(mf.lines())
    assert rows and not any(r["disabled"] for r in rows)


def test_partial_test_table_refused(mf, mtable):
    mf.go("--frames", "4", "--word", "0:7", table=mtable[1])
    assert any("TEST TABLE" in ln and "REFUSED" in ln and "give all of 12..21" in ln for ln in mf.lines())
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P for x in fr) and fr[-1]["fs2"] == fs2(cannot(R_TABLE_REFUSED), cannot(R_TABLE_REFUSED)), fr


# ---------------------------------------------------------------------------------------------------------------------
# Path 2 for the model programs (the fake EXE image holds their create sites)
# ---------------------------------------------------------------------------------------------------------------------
def test_path2_resolves_the_model_programs(mf):
    mf.go("--frames", "6", "--fake-exe", "--word", "0:7")
    assert any("model_filter: path 2: 10 of 10 programs resolved" in ln for ln in mf.lines())
    assert any("unit_filter: path 2: 12 of 12 programs resolved" in ln for ln in mf.lines())
    check_drops(mf.mframes(), 6, 3)
    t = mf.mtotals()
    assert t["path2"] == "resolved" and all(v["resolved"] == 1 and v["created"] == 0 for v in t["progs"].values()), t


@pytest.mark.parametrize("bad", ["mblob", "mstore"])
def test_path2_model_refusal_keeps_the_units(mf, bad):
    mf.go("--frames", "6", "--fake-exe", "--fake-bad", bad, "--word", "0:7")
    ref = [ln for ln in mf.lines() if "model_filter: path 2 REFUSED" in ln]
    assert len(ref) == 1, ref
    assert any("unit_filter: path 2: 12 of 12 programs resolved" in ln for ln in mf.lines())
    fr = mf.mframes()
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P for x in fr), fr
    assert fr[-1]["fs2"] == fs2(cannot(R_PATH2_REFUSED), cannot(R_PATH2_REFUSED)), hex(fr[-1]["fs2"])
    loud = [ln for ln in mf.lines() if "model_filter: LOUD" in ln]
    assert len(loud) == 2 and all("path 2 REFUSED" in ln for ln in loud), loud
    assert all(x["imm"] == UNITS_DROPPED for x in mf.uframes() if x["f"] >= 1)


def test_path2_shared_pattern_refuses_both(mf):
    mf.go("--frames", "5", "--fake-exe", "--fake-bad", "level", "--word", "0:7")
    assert any("model_filter: path 2 REFUSED" in ln for ln in mf.lines())
    assert any("unit_filter: path 2 REFUSED" in ln for ln in mf.lines())
    assert all(x["imm"] == ALL_P for x in mf.mframes())


# ---------------------------------------------------------------------------------------------------------------------
# Ticket 0719: the cached map (model_map_cache; unit_filter.cpp g_mapCache). A WRITE_DISCARD map of a DYNAMIC buffer that
# is not a constant buffer (here the instance stream, the skinned ids and the palette) gets a staging of ordinary memory;
# its Unmap forwards the pages written. The host's --map-check reads each buffer back from the driver after the frame's
# command list ran and compares it with what it wrote; the reader checks the records as everywhere above.
# ---------------------------------------------------------------------------------------------------------------------
MAP_MODES = {
    "whole": [],
    "split": ["--map-mode", "split", "--minst", "200"],   # WRITE_DISCARD of the first half, WRITE_NO_OVERWRITE of the rest
    "noover": ["--map-mode", "noover"],                   # initial data, only WRITE_NO_OVERWRITE: the direct path always
    "pad": ["--map-pad", "65536"],                        # 64 KB past the copy's mark, written every frame: forwarded too
    "def": ["--map-def"],                                 # the deferred context's own WRITE_DISCARD maps
}


def map_lines(mf):
    out = []
    for ln in mf.run.log_lines("host"):
        m = re.search(r"\] B (\d+) inst=(\S+) ids=(\S+) pal=(\S+)$", ln)
        if m:
            out.append(dict(f=int(m.group(1)), inst=m.group(2), ids=m.group(3), pal=m.group(4)))
    return out


@pytest.mark.parametrize("cache", ["0", "1", "verify"])
@pytest.mark.parametrize("mode", list(MAP_MODES))
def test_map_cache_keeps_buffers_and_records(mf, cache, mode):
    """Every key value and map pattern: the driver's buffers hold exactly what the host wrote (every frame), the drops
    and records are those of the direct path, and the counters say which path each map took."""
    mf.run.set_ini(model_map_cache=cache)
    mf.go("--frames", "8", "--word", "0:6", "--map-check", *MAP_MODES[mode])
    check_drops(mf.mframes(), 6, 3)
    b = map_lines(mf)
    assert [x["f"] for x in b] == list(range(8)), b
    assert all(x["inst"] == "ok" and x["ids"] == "ok" and x["pal"] == "ok" for x in b), b
    t = mf.mtotals()
    assert t["map_cache"] == {"0": 0, "1": 1, "verify": 2}[cache], t
    assert t["stage_fail"] == 0 and t["ww_fail"] == 0 and t["om_full"] == 0, t
    if cache == "0":
        assert t["map_cached"] == 0 and t["fwd_kb"] == 0 and t["stage_kb"] == 0 and t["map_direct"] > 0, t
        assert any("model_filter: model_map_cache=0" in ln for ln in mf.lines())
    else:
        assert t["map_cached"] > 0 and t["fwd_kb"] > 0 and t["stage_kb"] >= 64, t
    if cache == "verify":
        assert t["verify_kb"] > 0 and t["verify_bad"] == 0 and t["late_pages"] == 0, t
        assert t["rec_diff"] == 0 and "rec_kb" in t, t   # every record read equals the direct path's copy
        assert any("model_map_cache=verify - TEST ONLY" in ln for ln in mf.lines())
    if mode in ("split", "noover"):
        assert t["map_direct"] > 0, t   # the WRITE_NO_OVERWRITE maps
    if mode == "pad" and cache != "0":
        assert t["fwd_kb"] >= 4 * 64, t   # the pad's 64 KB in every cached frame (the copy's mark stays below it)


def test_map_cache_constant_buffers_stay_direct(mf):
    """cb0 (mapped per single draw on both contexts) and cb1 are constant buffers: never cached (the driver gives them
    cached memory already), so with the default key the direct maps outnumber the cached ones here."""
    mf.go("--frames", "6", "--word", "0:6", "--map-check")
    t = mf.mtotals()
    assert t["map_cache"] == 1 and t["map_cached"] > 0 and t["map_direct"] > t["map_cached"], t
    assert all(x["inst"] == "ok" and x["ids"] == "ok" and x["pal"] == "ok" for x in map_lines(mf))


# ---------------------------------------------------------------------------------------------------------------------
# Ticket 0788: the one-frame hold of a newly droppable key (research/05-graphics-composition/lategame-model-flash.md).
# The server draws frame P from the tap of Present P-1; a draw passed in frame P-1 and dropped in frame P was drawn by
# nobody in P. So the first frame a key is droppable its draws pass once more and are recorded HELD (the server draws
# that record in the next frame); they are dropped from the frame after on.
# ---------------------------------------------------------------------------------------------------------------------
LATCHED6 = [i for i, b in enumerate(CELL_BITS) if b & (CITIES | LANDMARKS)]


def hframes(mf):
    """Per frame: the HELD records of the frame's tap (the host's "H" line)."""
    out = {}
    for ln in mf.run.log_lines("host"):
        m = re.search(r"\] H (\d+) nheld=(\d+)", ln)
        if m:
            out[int(m.group(1))] = int(m.group(2))
    return out


def first_d(fr):
    """Per (context, cell): the first frame the cell was dropped in, or None."""
    return {(ctx, i): next((x["f"] for x in fr if x[ctx][i] == "D"), None) for ctx in ("imm", "def_") for i in range(9)}


def test_hold_delays_each_newly_droppable_key_one_frame(mf):
    """Hold on (the product's default): each key's draws are recorded HELD exactly once (both contexts, in the frame
    before their first drop), no cell is dropped in the frame its key is held, every DROPPED record still matches a
    dropped cell (the reader's check)."""
    mf.go("--frames", "9", "--word", "0:6", hold=True)
    fr, h = mf.mframes(), hframes(mf)
    check_drops(fr, 6, 4)
    fd = first_d(fr)
    assert all(fd[(c, i)] is not None for c in ("imm", "def_") for i in LATCHED6), fd
    assert all(fd[(c, i)] is None for c in ("imm", "def_") for i in range(9) if i not in LATCHED6), fd
    assert sum(h.values()) == 2 * len(LATCHED6), h
    for c in ("imm", "def_"):
        for i in LATCHED6:
            assert h.get(fd[(c, i)] - 1, 0) > 0, (c, i, fd, h)
    t = mf.mtotals()
    assert t["held"] == 2 * len(LATCHED6) and t["mismatch"] == 0 and t["place_miss"] == 0, t
    assert not [ln for ln in mf.lines() if "model_filter: LOUD" in ln], [ln for ln in mf.lines() if "LOUD" in ln]


def test_hold_against_the_old_rule(tmp_path, hidden_desktop, built, mtable):
    """The same run with C5_SHELL_MODEL_HOLD=0 (the diagnostic switch): each cell's first drop is exactly one frame earlier, no record is HELD,
    the counter stays 0 and the log names the test key."""
    first = {}
    for hold in (True, False):
        r = shellrun.Run(tmp_path / ("h%d" % hold), hidden_desktop, "mfhold%d" % hold)
        try:
            m = Mf(r, mtable[0])
            m.go("--frames", "9", "--word", "0:6", hold=hold)
            first[hold] = first_d(m.mframes())
            if not hold:
                assert sum(hframes(m).values()) == 0 and m.mtotals()["held"] == 0
                assert any("C5_SHELL_MODEL_HOLD=0 - DIAGNOSTIC" in ln for ln in m.lines())
            else:
                assert not any("C5_SHELL_MODEL_HOLD" in ln for ln in m.lines())
        finally:
            r.stop()
    for k, v in first[False].items():
        assert (first[True][k] is None) == (v is None), (k, first)
        if v is not None:
            assert first[True][k] == v + 1, (k, first)


def test_hold_again_after_the_bits_were_cleared(mf):
    """Cleared at frame 5 and latched again at frame 9: the keys were not droppable in the frames between, so each is held
    once more before its drops start again (two holds per key in the run)."""
    mf.go("--frames", "15", "--word", "0:6", "--word", "5:0", "--word", "9:6", hold=True)
    fr, h = mf.mframes(), hframes(mf)
    assert sum(h.values()) == 4 * len(LATCHED6), h
    assert all(x["imm"] == ALL_P and x["def_"] == ALL_P for x in fr if 6 <= x["f"] <= 10), fr
    assert any(x["imm"] == expect(6) for x in fr if x["f"] >= 11), fr
    # frames whose tap is new (while the bits are clear nothing is tapped and the host reads the last tap again)
    new = [x for i, x in enumerate(fr) if x["seq"] and (i == 0 or x["seq"] != fr[i - 1]["seq"])]
    assert len(new) >= 10 and all(x["check"] == "ok" for x in new), [(x["f"], x["seq"], x["check"]) for x in new if x["check"] != "ok"]


def test_hold_when_the_identity_set_arrives(mf):
    """The set published at frame 4 (no drops before): the first frame the keys are in the set they are held, so the drops
    start one frame later than in test_identity_set_late."""
    mf.go("--frames", "10", "--word", "0:6", "--set-at", "4", hold=True)
    fr, h = mf.mframes(), hframes(mf)
    assert all(x["imm"] == ALL_P for x in fr if x["f"] <= 5), fr
    assert all(x["imm"] == expect(6) and x["check"] == "ok" for x in fr if x["f"] >= 7), fr
    assert sum(h.values()) == 2 * len(LATCHED6), h
