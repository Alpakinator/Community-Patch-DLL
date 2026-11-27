"""Issue 4752 (epic 0040): the compositor's unit-draw filter (shell/composite/unit_filter.cpp), on its stand-in host
unit_filter_host.exe (shell/tests/unit_filter_host.cpp: a D3D11 program that plays the server's part of the main section,
loads shell_composite.dll and draws twelve cells on the immediate and twelve on a deferred context each frame, then reads
them back: P = drawn, D = dropped).

The host's shaders are its own (D3DCompile at run time); their SHA-256 digests reach the composite as a test table
(unit_filter_table, TEST ONLY) built here from the host's --dump files. Acceptance (a)-(g) of tracker/issues/
4752-compositor-unit-draw-filter.md:
  (a) the table's pairs are dropped on the immediate and the deferred context through every draw entry while the latched
      bit is 1, and passed while it is 0 or the switch says off;       test_word_schedule, test_switch_*, test_path2_resolves
  (b) unmarked draws, the mask pair, the selection pair, a DROP PS with a foreign VS and material 88's case always pass;
  (c) a word written in mid-frame takes effect at the next frame;      test_word_schedule
  (d) no live server (pid 0, another writer, stale heartbeat, no magic, no section, not composite): draws pass, counted;
  (e) an exception in a detour (draw, set, create, present) turns the filter alone off, logged once;
  (f) counters per program in the 1 s line and the totals;
  (g) unit_filter=auto|on|off (on/off TEST ONLY, logged), hook_units.
Path 2 (the EXE's renderer, unit-draw-filter.md 3.2) runs on a fake EXE image; every validation of it has a negative.
Issue 4761 (D118): the mode block's filterState word (can drop, or cannot drop with its reason) is checked on the good
paths (path 1, path 2, unit_filter=on, every server state) and on every way the filter cannot drop that the stand-in can
stage: each path-2 refusal, path 2 not run, an exception in each detour, unit_filter=off, a refused test table,
hook_units=0 with path 1 and with path 2 (check_fstate, test_kill_switch_with_path2_reports_no_hooks).

Run: `python -I shell/tests/test_unit_filter.py` (or through pytest) with C5_SHELL_BUILD set to a build of shell/build.cmd
(plain or asan). A missing binary fails (no skip).
"""
import hashlib
import os
import re
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import pytest  # noqa: E402

import shellrun  # noqa: E402

ALL_P = "P" * 12
DROPPED = "D" * 6 + "P" * 6           # cells 0-5 are the table's pairs, 6-11 always pass
SHORT = ["29a0b061", "82e59838", "60d270e1", "5def4f99", "0e7a3566", "8cb52b96", "72c27aea", "246527ed", "59af33ff",
         "9a5991d7", "da2382af", "8d853988"]
# per frame with the drop on: 6 immediate + 6 deferred cells + 1 DrawAuto dropped; cells 8 and 9 (a DROP PS with another VS)
# on both contexts pass while the bit is set
DROP_PER_FRAME = 13
PASSED_SET_PER_FRAME = 4
# the composite's ini for these runs: version-3 hand-over keys (no ring, no lockstep), the taps off (the fake EXE image holds
# none of their patterns), the stats line every 200 ms
KEYS = dict(handover="v3", lockstep="0", depth_write="0", tap="0", water_tap="0", overlay_tap="0", decal_tap="0",
            shadow_tap="0", ice_tap="0", cut_tap="0", model_fow="0", stats_ms="200")
# issue 4761: VPShellMode::filterState (shell_shared.h VPFILTER_*): bits 0-7 the state, bits 8-15 the reason
NOT_REPORTED, CAN = 0x0, 0x1
R_PATH2_REFUSED, R_PATH2_NOT_RUN, R_EXCEPTION, R_NO_DROP, R_MODE_OFF, R_TABLE_REFUSED, R_MARK_FULL, R_NO_HOOKS = range(1, 9)


def cannot(reason):
    return 0x2 | (reason << 8)


def check_fstate(u, want, first=0):
    """Every frame from `first` on carries filterState `want` in the mode block (the host reads it after Present), the
    totals line names the same state and reason, and the composite logged the state once when it took it."""
    fr = u.frames()
    assert fr and all(x["fstate"] == want for x in fr if x["f"] >= first), [(x["f"], hex(x["fstate"])) for x in fr]
    t = u.totals()
    assert (t["filter_state"], t["filter_reason"]) == (want & 0xFF, want >> 8), t
    if want:
        assert sum(("unit_filter: filterState 0x%X (" % want) in ln for ln in u.lines()) == 1, \
            [ln for ln in u.lines() if "filterState" in ln]
    return fr


def host_exe() -> Path:
    p = shellrun.bin_dir() / "unit_filter_host.exe"
    assert p.exists(), "unit_filter_host.exe not built in %s: run `cmd /c shell\\build.cmd`" % p.parent
    return p


@pytest.fixture(scope="session")
def table(tmp_path_factory, hidden_desktop):
    """The test table: the SHA-256 of the host's twelve program shaders, from its --dump files (computed here)."""
    d = tmp_path_factory.mktemp("ufdump")
    p = shellrun.launch([str(host_exe()), "--dump", str(d), "--log", str(d / "dump.log")], hidden_desktop)
    try:
        assert p.wait(60) == 0, shellrun.tail(d / "dump.log", 10)
    finally:
        p.kill()
        p.close()
    lines = []
    for i in range(12):
        lines.append("%d %s" % (i, hashlib.sha256((d / ("shader%02d.bin" % i)).read_bytes()).hexdigest()))
    digests = {hashlib.sha256((d / ("shader%02d.bin" % i)).read_bytes()).hexdigest() for i in range(14)}
    assert len(digests) == 14, "the host's 14 shaders must be distinct"
    t = d / "table.txt"
    t.write_text("# test table (4752)\n" + "\n".join(lines) + "\n", encoding="utf-8")
    return t


class Uf:
    """One host run: a private copy of the DLL and ini (shellrun.Run), the host as the section's server."""

    def __init__(self, run: shellrun.Run, table: Path):
        self.run = run
        self.table = table
        self.proc = None
        self.code = None
        run.set_ini(**KEYS)

    def go(self, *args, env=None, seconds=60, table=True):
        e = {"VPSHELL_LOG": str(self.run.composite_log), "VPSHELL_SECTION": self.run.section}
        if table:
            e["VPSHELL_UNIT_FILTER_TABLE"] = str(self.table)
        e.update(env or {})
        cmd = [str(host_exe()), "--dll", str(self.run.dll), "--log", str(self.run.host_log), "--section", self.run.section, *args]
        self.proc = shellrun.launch(cmd, self.run.desktop, env=e)
        self.code = self.proc.wait(seconds)
        if self.code is None:
            self.proc.kill()
        self.proc.close()
        assert self.code == 0, (self.code, shellrun.tail(self.run.host_log, 12), shellrun.tail(self.run.composite_log, 12))
        return self

    def frames(self):
        out = []
        for ln in self.run.log_lines("host"):
            m = re.search(r"\] F (\d+) imm=(\w+) def=(\w+) latched=(\d+) lpresent=(\d+) dropped=(\d+) passed_set=(\d+) "
                          r"fstate=0x([0-9A-F]+)", ln)
            if m:
                out.append(dict(f=int(m.group(1)), imm=m.group(2), def_=m.group(3), latched=int(m.group(4)),
                                lpresent=int(m.group(5)), dropped=int(m.group(6)), passed_set=int(m.group(7)),
                                fstate=int(m.group(8), 16)))
        return out

    def lines(self):
        return self.run.log_lines("composite")

    def uf_lines(self, what):
        return [ln for ln in self.lines() if ("unitfilter %s:" % what) in ln]

    def totals(self):
        t = self.uf_lines("totals")
        assert t, "no 'unitfilter totals:' line; composite log tail: %s" % shellrun.tail(self.run.composite_log, 12)
        return parse(t[-1])


def parse(ln):
    d = {}
    for k, v in re.findall(r"\b([a-z_0-9]+)=(\S+)", ln.split(" progs ")[0]):
        d[k] = int(v) if re.fullmatch(r"-?\d+", v) else v
    progs = {}
    if " progs " in ln:
        for name, c, m, r, f, s, dd, p in re.findall(r"\b([0-9a-f]{8})=c(\d+)/m(\d+)/r(\d+)/f(\d+)/s(\d+)/d(\d+)/p(\d+)", ln.split(" progs ")[1]):
            progs[name] = dict(created=int(c), marked=int(m), resolved=int(r), refused=int(f), seen=int(s), dropped=int(dd), passed=int(p))
    d["progs"] = progs
    return d


@pytest.fixture
def uf(tmp_path, hidden_desktop, built, request, table):
    r = shellrun.Run(tmp_path / "run", hidden_desktop, "uf" + request.node.name[-18:].replace("[", "_").replace("]", ""))
    u = Uf(r, table)
    yield u
    r.stop()


def check_cells(fr, expect_drop):
    """expect_drop(f) -> True when frame f's table cells must be dropped."""
    assert fr, "no frame lines"
    for x in fr:
        want = DROPPED if expect_drop(x["f"]) else ALL_P
        assert x["imm"] == want and x["def_"] == want, (x, want)


def no_composite_disabled(u):
    rows = shellrun.stats(u.lines())
    assert rows and not any(r["disabled"] for r in rows), [ln for ln in u.lines() if "DISABLED" in ln][:3]


# ---------------------------------------------------------------------------------------------------------------------
# ticket 4785: the unit shadow-mask pairs dropped while the word has VPMODE_UNITS and VPMODE_UNIT_SHADOWS (0x11); cell 7 is
# the material-90 mask pair (VS 8cb52b96, PS 72c27aea)
# ---------------------------------------------------------------------------------------------------------------------
MASK_DROPPED = "D" * 6 + "P" + "D" + "P" * 4


def test_mask_pairs_dropped_under_bit4(uf):
    uf.go("--frames", "16", "--word", "2:16:mid", "--word", "4:17:mid", "--word", "8:1:mid", "--word", "12:0:mid")
    fr = uf.frames()
    assert [x["f"] for x in fr] == list(range(16))
    # 0x10 alone (no unit bit) drops nothing; 0x11 drops the pass-0 pairs and the mask pair; 0x1 the pass-0 pairs only
    want = {f: ALL_P for f in range(16)}
    for f in range(5, 9):
        want[f] = MASK_DROPPED
    for f in range(9, 13):
        want[f] = DROPPED
    for x in fr:
        assert x["imm"] == want[x["f"]] and x["def_"] == want[x["f"]], (x, want[x["f"]])
    t = uf.totals()
    p = t["progs"]
    assert t["frames_mask_drop"] == 4 and t["dropped_mask"] == 4 * 2 and t["passed_set_mask"] == 0, t
    assert p["72c27aea"]["dropped"] == 4 * 2 and p["8cb52b96"]["dropped"] == 4 * 2 and t["mismatch"] == 0, p
    assert t["mask_drop_now"] == 0 and t["dropped"] == 8 * DROP_PER_FRAME, t
    # the other kept programs are never dropped
    for name in ("da2382af", "8d853988", "246527ed", "59af33ff", "9a5991d7"):
        assert p[name]["dropped"] == 0, (name, p[name])
    assert any("the units' pass-0 and shadow-mask draws are dropped" in ln for ln in uf.lines())
    check_fstate(uf, CAN)
    no_composite_disabled(uf)


def test_mask_pairs_not_dropped_with_unit_filter_on(uf):
    """unit_filter=on (TEST ONLY) drops the pass-0 pairs whatever the word says, but the mask pairs only under bit 4 of a
    live server's word."""
    uf.run.set_ini(unit_filter="on")
    uf.go("--frames", "8")
    fr = uf.frames()
    assert fr and all(x["imm"] == DROPPED and x["def_"] == DROPPED for x in fr[1:]), fr
    t = uf.totals()
    assert t["dropped_mask"] == 0 and t["frames_mask_drop"] == 0, t


# ---------------------------------------------------------------------------------------------------------------------
# (a) (b) (c) (f): path 1 (creation hooks), the word written in mid-frame
# ---------------------------------------------------------------------------------------------------------------------
def test_word_schedule(uf):
    uf.go("--frames", "12", "--word", "4:1:mid", "--word", "8:0:mid")
    fr = uf.frames()
    assert [x["f"] for x in fr] == list(range(12))
    # (c) written in frame 4 after the immediate cells: frame 4 is all drawn, 5-8 dropped; cleared in frame 8: 9- drawn
    check_cells(fr, lambda f: 5 <= f <= 8)
    assert [x["latched"] for x in fr] == [0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0]
    assert fr[-1]["dropped"] == 4 * DROP_PER_FRAME and fr[-1]["passed_set"] == 4 * PASSED_SET_PER_FRAME, fr[-1]
    assert fr[5]["lpresent"] > fr[4]["lpresent"] > 0
    t = uf.totals()
    assert t["path2"] == "not-run" and t["off"] == 0 and t["mismatch"] == 0 and t["untracked"] == 0, t
    for name in SHORT:   # path 1: each program created once and marked once
        assert t["progs"][name]["created"] == 1 and t["progs"][name]["marked"] == 1 and t["progs"][name]["resolved"] == 0, (name, t["progs"][name])
    p = t["progs"]
    assert p["60d270e1"]["dropped"] == 4 * 7 and p["5def4f99"]["dropped"] == 4 * 4 and p["0e7a3566"]["dropped"] == 4 * 2, p
    assert t["dropped"] == 4 * DROP_PER_FRAME and t["passed_set"] == 4 * PASSED_SET_PER_FRAME
    # (b) the kept programs are never dropped
    for name in ("8cb52b96", "72c27aea", "da2382af", "8d853988", "246527ed", "59af33ff", "9a5991d7"):
        assert p[name]["dropped"] == 0, (name, p[name])
    assert p["72c27aea"]["passed"] == 12 * 2 and p["8d853988"]["passed"] == 12 * 2
    # every draw entry hooked and counted (S6), on both contexts
    assert (t["dii"], t["di"], t["d"], t["dinst"], t["diiind"], t["dinstind"], t["dauto"]) == (12 * 14, 24, 24, 24, 24, 24, 12), t
    # (f) the 1 s lines add up to the totals
    ones = [parse(ln) for ln in uf.uf_lines("1s")]
    assert len(ones) >= 2 and sum(o["dropped"] for o in ones) == t["dropped"]
    assert sum(o["progs"]["60d270e1"]["seen"] for o in ones) == p["60d270e1"]["seen"] == 12 * 11
    assert sum("path 2 not run" in ln for ln in uf.lines()) == 1
    # issue 4761: the good path (path 1) reports "can drop" from the first Present, with the bit 0 and 1 alike
    check_fstate(uf, CAN)
    assert t["frames_unable"] == 0 and not [ln for ln in uf.lines() if "unit_filter: LOUD:" in ln]
    no_composite_disabled(uf)


# ---------------------------------------------------------------------------------------------------------------------
# (d) no live server: draws pass, counted by reason; a recovery latches at the next Present
# ---------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("server,reason", [("pid0", "ignored_pid"), ("pidother", "ignored_pid"), ("stale", "ignored_stale"),
                                           ("nomagic", "ignored_notinit")])
def test_no_live_server_passes(uf, server, reason):
    uf.go("--frames", "6", "--server", server, "--word", "0:1")
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    assert t[reason] >= 6 and t["dropped"] == 0 and t["passed_set"] == 0 and t["frames_set"] == 0, t
    assert all(x["latched"] == 0 for x in uf.frames())
    check_fstate(uf, CAN)   # 4761: the state does not depend on the server's liveness (the latch does)


@pytest.mark.parametrize("server", ["zeroblock", "ok", "pidother", "stale", "none"])
def test_shipped_default_never_drops_without_the_bit(uf, server):
    """MC #4116 (a): the shipped unit_filter=auto with ServerUnits=0, i.e. a mode block that is absent (zeroblock: all
    zero, as every producer before 4753 leaves it; none: no section at all) or present with the word 0 (a live server;
    another writer; a stale heartbeat): every pass-0 unit draw passes for the whole run, and the dropped and
    passedSet counters (the 1 s lines, the totals, the mode block's droppedTotal / passedSetTotal) stay 0."""
    shipped = shellrun.SHELL.joinpath("composite", "shell_composite.ini").read_text(encoding="utf-8")
    assert len(re.findall(r"(?m)^unit_filter=auto\s*$", shipped)) == 1
    uf.go("--frames", "8", "--server", server)
    fr = uf.frames()
    check_cells(fr, lambda f: False)
    assert all(x["latched"] == 0 and x["dropped"] == 0 and x["passed_set"] == 0 for x in fr), fr
    t = uf.totals()
    assert t["mode"] == "auto" and t["dropped"] == 0 and t["passed_set"] == 0 and t["frames_set"] == 0 and t["frames_drop"] == 0, t
    assert all(o["dropped"] == 0 and o["passed_set"] == 0 for o in map(parse, uf.uf_lines("1s")))
    assert all(v["dropped"] == 0 for v in t["progs"].values())
    assert t["progs"]["60d270e1"]["marked"] == 1 and t["progs"]["60d270e1"]["passed"] == 8 * 11   # recognised, and passed
    # and nothing LOUD (path 2's "not run" on the stand-in is a plain line; LOUD only when a latch asks for drops)
    assert not [ln for ln in uf.lines() if "unit_filter: LOUD:" in ln], [ln for ln in uf.lines() if "LOUD" in ln]
    assert t["frames_unable"] == 0, t
    # 4761: the mode block carries "can drop" (no section: nothing is written, the host reads 0)
    fr = uf.frames()
    if server == "none":
        assert all(x["fstate"] == NOT_REPORTED for x in fr), fr
        assert (t["filter_state"], t["filter_reason"]) == (CAN, 0), t   # computed all the same, only not written
    else:
        check_fstate(uf, CAN)


def test_no_section_passes(uf):
    uf.go("--frames", "5", "--server", "none")
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    assert t["dropped"] == 0 and t["frames_set"] == 0, t


def test_not_composite_passes(uf):
    uf.run.set_ini(mode="passthrough")
    uf.go("--frames", "5", "--word", "0:1")
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    assert t["ignored_notcomposite"] >= 5 and t["dropped"] == 0, t


# ---------------------------------------------------------------------------------------------------------------------
# Issue 4761 (4757 #4248): handover=ring, the product's form since 0490. The ring sink keeps the main header's magic 0
# and writes the liveness only into the <section>.Frame section's server block; the latch must read that block.
# ---------------------------------------------------------------------------------------------------------------------
def check_follows_latch(fr):
    """Frame f's table cells are dropped exactly when the latch of frame f-1's Present had the bit (frame 0: never)."""
    assert fr, "no frame lines"
    for i, x in enumerate(fr):
        want = DROPPED if i > 0 and fr[i - 1]["latched"] == 1 else ALL_P
        assert x["imm"] == want and x["def_"] == want, (x, want, fr[i - 1] if i else None)


@pytest.mark.parametrize("ring", ["ok", "rebuilding"])
def test_ring_live_latches_and_drops(uf, ring):
    uf.run.set_ini(handover="ring")
    uf.go("--frames", "8", "--ring", ring, "--word", "0:1")
    fr = uf.frames()
    check_follows_latch(fr)
    # the frame section is mapped at the composite's first Present on the ring path at the latest: latched from frame 1 on
    assert all(x["latched"] == 1 for x in fr[1:]), fr
    t = uf.totals()
    n = sum(1 for i in range(1, len(fr)) if fr[i - 1]["latched"] == 1)
    assert n >= 6 and t["dropped"] == n * DROP_PER_FRAME and t["ignored_notinit"] == 0 and t["ignored_pid"] == 0, (n, t)
    assert any("frame section" in ln and "ring=%s" % ring in ln for ln in uf.run.log_lines("host"))
    assert any("handover=ring" in ln for ln in uf.lines())
    no_composite_disabled(uf)


@pytest.mark.parametrize("ring,reason", [("stale", "ignored_stale"), ("pidother", "ignored_pid"), ("failed", "ignored_srvstate"),
                                         ("stopped", "ignored_srvstate"), ("nomagic", "ignored_notinit")])
def test_ring_not_live_passes(uf, ring, reason):
    uf.run.set_ini(handover="ring")
    uf.go("--frames", "6", "--ring", ring, "--word", "0:1")
    fr = uf.frames()
    check_cells(fr, lambda f: False)
    assert all(x["latched"] == 0 for x in fr), fr
    t = uf.totals()
    assert t[reason] >= 5 and t["dropped"] == 0 and t["frames_set"] == 0, t
    assert any(("server %s)" % reason.split("_")[1]) in ln and "latched word 0x0" in ln for ln in uf.lines()), \
        [ln for ln in uf.lines() if "latched word" in ln]


def test_ring_stale_then_alive(uf):
    uf.run.set_ini(handover="ring")
    uf.go("--frames", "8", "--ring", "stale", "--stale-until", "3", "--word", "0:1")
    fr = uf.frames()
    check_follows_latch(fr)
    assert [x["latched"] for x in fr[:3]] == [0, 0, 0] and all(x["latched"] == 1 for x in fr[3:]), fr
    t = uf.totals()
    assert t["ignored_stale"] >= 3 and t["dropped"] == 4 * DROP_PER_FRAME, t


def test_v3_reads_the_main_header_not_the_ring_block(uf):
    """Negative of the source choice: with handover=v3 a live ring block does not make the server live (the main header,
    magic 0 in the ring form, decides)."""
    uf.run.set_ini(handover="v3")
    uf.go("--frames", "5", "--ring", "ok", "--word", "0:1")
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    assert t["ignored_notinit"] >= 5 and t["dropped"] == 0, t


def test_stale_then_alive(uf):
    uf.go("--frames", "8", "--server", "stale", "--stale-until", "3", "--word", "0:1")
    check_cells(uf.frames(), lambda f: f >= 4)   # the heartbeat is fresh from frame 3: latched at its Present
    t = uf.totals()
    assert t["ignored_stale"] >= 3 and t["dropped"] == 4 * DROP_PER_FRAME, t
    assert any("server stale" in ln and "latched word 0x0" in ln for ln in uf.lines())


# ---------------------------------------------------------------------------------------------------------------------
# (g) the switch and the kill switch
# ---------------------------------------------------------------------------------------------------------------------
def test_switch_off_passes_and_counts(uf):
    uf.run.set_ini(unit_filter="off")
    uf.go("--frames", "5", "--word", "0:1")
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    # written at frame 0's start, latched at its Present: frames 1-4 run with the bit set
    assert t["mode"] == "off" and t["dropped"] == 0 and t["passed_set"] == 4 * (DROP_PER_FRAME + PASSED_SET_PER_FRAME), t
    assert any("config: unit_filter=off - TEST ONLY" in ln for ln in uf.lines())
    # 4761: off cannot drop: the server, holding the latched bit, ends (D118); off asks for no drops, so no frames_unable
    check_fstate(uf, cannot(R_MODE_OFF))
    assert t["frames_unable"] == 0, t
    assert any("filterState 0x502 (cannot drop: unit_filter=off; the server's VPMODE_UNITS is latched" in ln for ln in uf.lines())


def test_switch_on_drops_without_word(uf):
    uf.run.set_ini(unit_filter="on")
    uf.go("--frames", "5")
    check_cells(uf.frames(), lambda f: f >= 1)   # the first frame runs on the latch before any Present: 0
    t = uf.totals()
    assert t["mode"] == "on" and t["dropped"] == 4 * DROP_PER_FRAME and t["passed_set"] == 0, t
    assert any("config: unit_filter=on - TEST ONLY" in ln for ln in uf.lines())
    check_fstate(uf, CAN)


def test_switch_bad_value_is_auto(uf):
    uf.run.set_ini(unit_filter="sometimes")
    uf.go("--frames", "5", "--word", "1:1")
    check_cells(uf.frames(), lambda f: f >= 2)
    assert any("CONFIG ERROR unit_filter='sometimes'" in ln for ln in uf.lines())
    assert uf.totals()["mode"] == "auto"


def test_kill_switch_hooks_nothing(uf):
    uf.run.set_ini(hook_units="0")
    uf.go("--frames", "5", "--word", "0:1", "--wait-hooks", "present")
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    assert t["dii"] == 0 and t["dropped"] == 0 and all(v["created"] == 0 for v in t["progs"].values()), t
    assert any(re.search(r"\bhooks: .*\bunits=0\b", ln) for ln in uf.lines())
    assert not any(re.search(r"hook ID3D11DeviceContext::DrawIndexedInstanced var \d .*enable queued", ln) for ln in uf.lines())
    # 4761: path 1 marks nothing without its hooks (path 2 not run on the stand-in) during the first two Presents of the
    # filter (the host's wait for the Present hook may already have made them), then no hooked call at all: "no draw
    # reached the hooks" from the filter's third Present on
    fr = uf.frames()
    assert all(x["fstate"] in (cannot(R_PATH2_NOT_RUN), cannot(R_NO_HOOKS)) for x in fr[:2]), fr
    check_fstate(uf, cannot(R_NO_HOOKS), first=2)
    loud = [ln for ln in uf.lines() if "unit_filter: LOUD:" in ln]
    assert len(loud) == 1 and "cannot drop" in loud[0] and re.search(r"filterState 0x[28]02\)", loud[0]), loud
    assert t["frames_unable"] >= 4, t


def test_kill_switch_with_path2_reports_no_hooks(uf):
    """4761: path 2 resolves every program on the fake image, but hook_units=0 leaves no hook to drop with: not
    reported for the filter's first two Presents (the hooks may still be going in), then cannot drop, no hooks; LOUD
    once. Before 4761 this state drew the units twice without a word (path 2 resolved, nothing LOUD)."""
    uf.run.set_ini(hook_units="0")
    uf.go("--frames", "6", "--fake-exe", "--word", "0:1", "--wait-hooks", "present")
    check_cells(uf.frames(), lambda f: False)
    fr = uf.frames()
    assert all(x["fstate"] in (NOT_REPORTED, cannot(R_NO_HOOKS)) for x in fr[:2]), fr
    check_fstate(uf, cannot(R_NO_HOOKS), first=2)
    assert sum("unit_filter: filterState 0x0 (not reported yet)" in ln for ln in uf.lines()) == 1
    t = uf.totals()
    assert t["path2"] == "resolved" and t["dropped"] == 0 and t["dii"] == 0 and t["frames_unable"] >= 4, t
    loud = [ln for ln in uf.lines() if "unit_filter: LOUD:" in ln]
    assert len(loud) == 1 and "no draw or shader bind has reached the filter's hooks" in loud[0] and "filterState 0x802" in loud[0], loud


# ---------------------------------------------------------------------------------------------------------------------
# (e) an exception inside a detour: the filter alone goes off, logged once
# ---------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("where,name", [("draw", "draw"), ("set", "SetShader"), ("create", "Create"), ("present", "Present")])
def test_fault_turns_the_filter_off(uf, where, name):
    uf.go("--frames", "6", "--word", "0:1", env={"VPSHELL_UNIT_FILTER_FAULT": where})
    check_cells(uf.frames(), lambda f: False)
    loud = [ln for ln in uf.lines() if "unit_filter: LOUD: EXCEPTION" in ln]
    assert len(loud) == 1 and name in loud[0], loud
    t = uf.totals()
    assert t["off"] == 1 and "exception" in t["off_why"] and t["dropped"] == 0, t
    # 4761: cannot drop, an exception, from the Present after the fault on (a Present fault leaves its own Present
    # unwritten; frame 0 is not checked for that reason)
    check_fstate(uf, cannot(R_EXCEPTION), first=1)
    assert t["frames_unable"] >= 4, t
    no_composite_disabled(uf)


def test_bad_test_table_turns_the_filter_off(uf, tmp_path):
    bad = tmp_path / "bad_table.txt"
    bad.write_text("0 00\n", encoding="utf-8")
    uf.go("--frames", "4", "--word", "0:1", env={"VPSHELL_UNIT_FILTER_TABLE": str(bad)})
    check_cells(uf.frames(), lambda f: False)
    assert any("TEST TABLE" in ln and "REFUSED" in ln for ln in uf.lines())
    assert uf.totals()["off"] == 1
    check_fstate(uf, cannot(R_TABLE_REFUSED))   # 4761


def test_without_test_table_nothing_is_marked(uf):
    """Negative of the table: the real digests name the EXE's programs, which the host does not create."""
    uf.go("--frames", "4", "--word", "0:1", table=False)
    check_cells(uf.frames(), lambda f: False)
    t = uf.totals()
    assert all(v["created"] == 0 and v["marked"] == 0 for v in t["progs"].values()) and t["dropped"] == 0, t
    loud = [ln for ln in uf.lines() if "unit_filter: LOUD:" in ln]
    assert len(loud) == 1 and "cannot drop" in loud[0] and "path 2 not run" in loud[0], loud
    assert t["frames_unable"] >= 3, t
    check_fstate(uf, cannot(R_PATH2_NOT_RUN))   # 4761


# ---------------------------------------------------------------------------------------------------------------------
# Path 2: the EXE's renderer (fake image)
# ---------------------------------------------------------------------------------------------------------------------
def test_path2_resolves(uf):
    uf.go("--frames", "8", "--fake-exe", "--word", "3:1:mid")
    check_cells(uf.frames(), lambda f: f >= 4)
    t = uf.totals()
    assert t["path2"] == "resolved" and t["off"] == 0, t
    for name in SHORT:   # created before the composite was loaded: path 1 saw none
        assert t["progs"][name]["resolved"] == 1 and t["progs"][name]["created"] == 0, (name, t["progs"][name])
    assert t["dropped"] == 4 * DROP_PER_FRAME
    assert sum("path 2: 12 of 12 programs resolved" in ln for ln in uf.lines()) == 1
    assert any(re.search(r"camera: EXE image at 0x[0-9A-F]+ \(VPSHELL_EXE_BASE", ln) for ln in uf.lines())
    check_fstate(uf, CAN)   # 4761: the good path of path 2
    assert t["frames_unable"] == 0, t


@pytest.mark.parametrize("bad,why", [
    ("level", "shader level 9, not 10: it created the 4_1 variants"),
    # ticket 4965 (epic 0590): addresses are build-table symbols; the site patterns (push of the blob, handle store) are
    # checked before any program is resolved
    ("blob", "PS 60d270e1: the blob uf_ps_60d270e1_blob"),
    ("push", "pattern 'PS 5def4f99: push blob, create call at +0x0B, handle store at +0x20' at uf_ps_5def4f99_push does not "
             "match on Steam build 4390913"),
    ("call", "VS 82e59838: the create call at uf_vs_82e59838_push+0xB does not call uf_create_vs"),
    ("store", "pattern 'VS 8cb52b96: push blob, create call at +0x0B, handle store at +0x20' at uf_vs_8cb52b96_push does not "
              "match on Steam build 4390913"),
    ("pattern", "pattern 'CreatePixelShader: mov [4*ecx+PS objects], edx' at uf_ps_store does not match on Steam build 4390913"),
    ("handle", "PS 0e7a3566: handle 362 at uf_ps_0e7a3566_handle is not below the renderer's PS count 362"),
    ("nullobj", "PS 72c27aea: handle"),
    ("stage", "is not a pixel shader"),
    ("vsptr", "VS 29a0b061: handle 40 was created from"),
    ("device", "is not the target's device"),
])
def test_path2_refusals(uf, bad, why):
    uf.go("--frames", "5", "--fake-exe", "--fake-bad", bad, "--word", "0:1")
    check_cells(uf.frames(), lambda f: False)
    ref = [ln for ln in uf.lines() if "unit_filter: path 2 REFUSED" in ln]
    assert len(ref) == 1 and why in ref[0], ref
    # LOUD once, at the first latch that asks for drops, naming the refusal
    loud = [ln for ln in uf.lines() if "unit_filter: LOUD:" in ln]
    assert len(loud) == 1 and "the latch asks for drops (the server's VPMODE_UNITS)" in loud[0] and why in loud[0], loud
    t = uf.totals()
    assert t["path2"] == "refused" and t["off"] == 1 and t["dropped"] == 0 and t["frames_unable"] >= 4, t
    assert all(v["resolved"] == 0 and v["marked"] == 0 for v in t["progs"].values()), t["progs"]
    if bad == "nullobj":
        assert "holds no object" in ref[0]
    # what the server reads in the mode block (unit-draw-filter.md 3.3, the D118 route): the latched bit set while
    # droppedTotal stays 0, every frame after the first latch
    fr = uf.frames()
    assert all(x["latched"] == 1 and x["dropped"] == 0 for x in fr), fr
    # 4761: and the word that makes it D118: cannot drop, path 2 refused, on every frame with the bit latched; the
    # composite's note names the server's step
    check_fstate(uf, cannot(R_PATH2_REFUSED))
    assert any("unit_filter: filterState 0x102 (cannot drop: path 2 refused; the server's VPMODE_UNITS is latched: the server "
               "takes the D125 step (D118))" in ln for ln in uf.lines()) or \
        any("unit_filter: filterState 0x102 (cannot drop: path 2 refused) at present" in ln for ln in uf.lines())
    no_composite_disabled(uf)


if __name__ == "__main__":
    os.environ.setdefault("PYTEST_DISABLE_PLUGIN_AUTOLOAD", "1")
    sys.exit(pytest.main([str(Path(__file__)), "-p", "no:cacheprovider", "-q"] + sys.argv[1:]))
