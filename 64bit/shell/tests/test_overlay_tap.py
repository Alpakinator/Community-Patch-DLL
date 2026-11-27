"""Issue 3703 (epic 0167): the Terrain Overlay target tap of shell_composite.dll (composite/overlay_tap.cpp) with its
reader in server.exe (server/src/render/srv_overlay_tap.cpp, run as test_reader_tool.exe, C5_OVERLAY_READER), on the
stand-in (hidden desktop, rule 14). Model: test_water_tap.py.

test_host --fake-exe (the main section is created here) with overlay_test=<mode>: a synthetic EXE in the DLL's memory
(handle, view table, g_NumViews, OverlayProj and a real 2048 x 2048, 12-mip R8G8B8A8_TYPELESS target with known
texels; overlay_tap.cpp BuildTestWorld) runs through the real resolution, slot, copy and publish code. Every Present the
test world writes frame k into texel (0,0) of every mip and into OverlayProj view 0 element 12, so the reader checks
that texels and matrices of a bound pair are one frame (decision 2 (i)), over many pairs while the producer copies at
the stand-in's full frame rate. Checked: the pair exact and consecutive; the verify mode (a second read of the target
in the same Present equals the copy, every mip); every install failure (handle, view object, size, mips, format, views,
fault, section) -> FAILED with its reason, LOUD once, and the reader's Failed with that reason; faults in play (the
target gone, a matrix the reader refuses) -> LOUD, counted, the game goes on, the reader keeps the last pair and counts
the stale episode; the kill switch before and during copies; the map-frame gate; the real path refusing the fake EXE
and an unvalidated one; a static check of the tap's byte patterns against the EXE on disk.

Needs C5_SHELL_BUILD (shell/build.cmd) and C5_OVERLAY_READER (server/tests/render/overlay/test_reader_build.py). A
missing binary fails the test (no skip).
"""
import json
import os
import re
import struct
import subprocess
import time
from pathlib import Path

import pytest

import exe_golden
import shellrun
import tapcheck

HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")
OVL_OFFSET, OVL_SIZE = 3904, 112
STATES = ["NONE", "ACTIVE", "FAILED", "OFF"]
FAILS = ["NONE", "PATTERN", "HANDLE", "DESC", "SHARED", "EXCEPTION", "VIEWS", "KILLED", "VERSION"]
KNOWN_TEXEL = ((100 * 7 + 37 * 13) & 0xFF) | (0x40 << 24)     # TestTexel(100, 37, mip 0) of overlay_tap.cpp


def reader_exe():
    p = os.environ.get("C5_OVERLAY_READER", "")
    assert p and Path(p).exists(), ("C5_OVERLAY_READER must name test_reader_tool.exe "
                                    "(python -I server/tests/render/overlay/test_reader_build.py --out DIR)")
    return p


def reader(section, *args, timeout=90):
    p = subprocess.run([reader_exe(), "--section", section, *args], capture_output=True, text=True, timeout=timeout,
                       creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def parse_block(raw):
    f = struct.unpack_from("<8I", raw, 0)
    d = dict(seq=f[0], state=f[1], failReason=f[2], exePid=f[3], slotCount=f[4], newestSlot=f[5], generation=f[6], loadSeq=f[7])
    d["copiesTotal"], d["copyMsTotal"], d["copyMsMax"] = struct.unpack_from("<Qff", raw, 32)
    d["inUseSlot"], d["ackGeneration"], d["ackServerPid"], d["missedTotal"], d["faultsTotal"] = struct.unpack_from("<5I", raw, 48)
    d["stateName"] = STATES[d["state"]] if d["state"] < len(STATES) else "?"
    d["failName"] = FAILS[d["failReason"]] if d["failReason"] < len(FAILS) else "?"
    return d


class OverlayRun:
    def __init__(self, run, ini, seconds=20, host=None):
        self.run = run
        self.main = tapcheck.Mapping(run.section, create=4096)
        self.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", overlay_tap="1",
                   overlay_need_scene="0", overlay_fail_frames="5", water_tap="0")
        cfg.update(ini)
        run.set_ini(**cfg)
        run.start_host(*(host or HOST), seconds=seconds)

    def block(self):
        raw = tapcheck.read_block(self.main, OVL_OFFSET, OVL_SIZE)
        return parse_block(raw) if raw else {}

    def wait(self, pred, what, timeout_s=20.0):
        t_end = time.time() + timeout_s
        b = {}
        while time.time() < t_end:
            b = self.block()
            if b and pred(b):
                return b
            time.sleep(0.02)
        raise TimeoutError("overlay tap: %s not reached: %s; log tail %s" % (what, b, shellrun.tail(self.run.composite_log, 10)))

    def wait_state(self, state, timeout_s=20.0):
        return self.wait(lambda b: b["stateName"] == state, "state " + state, timeout_s)

    def close(self):
        self.main.close()


@pytest.fixture
def orun(run):
    made = []

    def make(ini, seconds=20, host=None):
        o = OverlayRun(run, ini, seconds, host)
        made.append(o)
        return o
    yield make
    for o in made:
        o.close()


def finish(run, seconds=40):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)
    rows = shellrun.stats(run.log_lines())
    assert rows and not any(r["disabled"] for r in rows)
    assert sum(r["frames"] for r in rows) > 0


def olines(run):
    return [ln for ln in run.log_lines() if "overlay_tap:" in ln]


def test_pairs_exact_and_consecutive(run, orun, tmp_path):
    """60 pairs read while the producer copies at the stand-in's full rate: every pair's texels (all mips' frame stamp)
    and OverlayProj are of one frame; generations, frames and presentCounts only grow; the known texels survive."""
    o = orun(dict(overlay_test="ok"), seconds=30)
    a = o.wait_state("ACTIVE")
    assert a["exePid"] == run.host.pid and a["slotCount"] == 3 and a["failName"] == "NONE"
    js = tmp_path / "r.json"
    rc, txt = reader(run.section, "--pairs", "60", "--seconds", "20", "--readback", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["mismatches"] == 0 and r["state"] == "Active" and r["refused"] == 0, r
    ps = r["pairs"]
    assert len(ps) == 60
    for p in ps:
        assert p["numViews"] == 2 and p["texelKnown"] == KNOWN_TEXEL and (p["texel0"] & 0xFFFF) == (p["k"] & 0xFFFF), p
        assert p["proj0"][0] == pytest.approx(2 / 832) and p["proj0"][15] == 1.0
    for x, y in zip(ps, ps[1:]):
        assert y["generation"] > x["generation"] and y["k"] > x["k"] and y["presentCount"] > x["presentCount"], (x, y)
    assert {p["slot"] for p in ps} <= {0, 1, 2} and len({p["slot"] for p in ps}) >= 2
    b = o.block()
    assert b["copiesTotal"] >= 60 and b["copyMsTotal"] > 0 and b["faultsTotal"] == 0
    assert b["ackGeneration"] == ps[-1]["generation"] and b["inUseSlot"] == 0xFFFFFFFF   # the reader let go at exit
    run.wait_log(r"overlay_tap: INSTALLED: first copy published \(slot \d, generation 1, Present \d+, 2 views", timeout_s=2)
    run.wait_log(r"overlay_tap: 3 slots of 2048x2048, 12 mips, format 27 created \(NT-shared", timeout_s=2)
    finish(run)
    # at the stand-in's uncapped rate (thousands of Presents a second) the previous copy is often still in flight at the
    # next Present: since 0194 (MC 2026-11-14 20:08) those are a counted lag, the first 3 logged, never LOUD; no LOUD
    # line at all, no failure
    bad = [ln for ln in olines(run) if "LOUD" in ln or "FAILED" in ln]
    assert not bad, bad
    inflight = [ln for ln in olines(run) if "missed copy (lag, counted, D132): the previous copy is still in flight" in ln]
    assert 1 <= len(inflight) <= 3, inflight
    assert len(inflight) < 3 or "further ones are counted only" in inflight[2], inflight
    assert len(inflight) < 3 or b["missedTotal"] > 3, b


def test_verify_mode_second_read_equals_the_copy(run, orun):
    o = orun(dict(overlay_test="ok", overlay_verify="7"), seconds=15)
    o.wait_state("ACTIVE")
    line = run.wait_log(r"overlay_tap: VERIFY copy \d+ .*totals tex [3-9] ok", timeout_s=12)
    lines = [ln for ln in olines(run) if "VERIFY copy" in ln]
    assert len(lines) >= 3
    for ln in lines:
        assert re.search(r": 22369620 bytes over 12 mips, 0 differ;", ln), ln
        assert "no bind of the target seen" in ln, ln    # the stand-in never binds the target (live: the EXE does)
    assert " 0 bad, proj" in line
    finish(run)


@pytest.mark.parametrize("mode,fail,text", [
    ("nullhandle", "HANDLE", r"handle \[ovl_handle\] = 0x0 \(no target\) \(for 5 map frames\)"),
    ("badvtbl", "HANDLE", r"handle 0x24: no D3D11 texture view in its table entry"),
    ("wrongsize", "DESC", r"the texture is 1024x1024, 11 mips, array 1, 1 samples, format 27"),
    ("wrongmips", "DESC", r"the texture is 2048x2048, 1 mips"),
    ("wrongformat", "DESC", r"format 90 \(expected 2048 x 2048, 12 mips, R8G8B8A8\)"),   # B8G8R8A8_TYPELESS
    ("views0", "VIEWS", r"g_NumViews = 0 \(1\.\.4\)"),
    ("views9", "VIEWS", r"g_NumViews = 9 \(1\.\.4\)"),
    ("fault", "EXCEPTION", r"fault reading the view object 0x[0-9A-F]+ of handle 0x24"),
])
def test_install_failure_is_loud_and_final(run, orun, mode, fail, text):
    """Decision 5 / MC #701: FAILED with its reason, one LOUD line, the reader Failed with that reason (the server's
    render start fails with its text); the host and the composite run on; no slot texture is made."""
    o = orun(dict(overlay_test=mode))
    b = o.wait_state("FAILED")
    assert b["failName"] == fail and b["copiesTotal"] == 0, b
    run.wait_log(r"overlay_tap: LOUD: FAILED %s at the install: .*%s" % (fail, text), timeout_s=5)
    rc, txt = reader(run.section, "--seconds", "2")
    assert rc == 1 and "the overlay tap could not install: VPOVL_FAILED %s" % fail in txt, txt
    time.sleep(0.5)
    assert sum("LOUD: FAILED" in ln for ln in olines(run)) == 1
    assert not any("slots of" in ln for ln in olines(run))
    finish(run)


def test_section_failure_is_shared(run, orun):
    o = orun(dict(overlay_test="ok", overlay_section="Local\\VPShellOverlay\\no\\such"))
    b = o.wait_state("FAILED")
    assert b["failName"] == "SHARED"
    run.wait_log(r"overlay_tap: CreateFileMapping\('Local\\VPShellOverlay\\no\\such', 1072\) failed", timeout_s=5)
    run.wait_log(r"overlay_tap: LOUD: FAILED SHARED at the install: the overlay section could not be created", timeout_s=2)
    finish(run)


def test_target_lost_in_play_is_counted_and_the_game_goes_on(run, orun, tmp_path):
    """overlay_test=lost: the handle becomes 0 at test frame 40, after the install. The block stays ACTIVE with the last
    copy, missed and faults count up, LOUD lines are rate-limited; the reader keeps its pair and counts one stale
    episode."""
    o = orun(dict(overlay_test="lost"), seconds=20)
    o.wait_state("ACTIVE")
    b = o.wait(lambda b: b["faultsTotal"] > 50, "faults counted")
    assert b["stateName"] == "ACTIVE" and b["missedTotal"] >= b["faultsTotal"] - 1, b
    gen = b["generation"]
    js = tmp_path / "r.json"
    rc, txt = reader(run.section, "--pairs", "0", "--seconds", "3", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["stale"] == 1 and r["accepted"] == 1 and r["pairs"][0]["generation"] == gen, r
    assert "stale (lag, counted, D132): no new copy for" in txt   # MC #891: the reader counts the lag; the fault is LOUD below
    loud = [ln for ln in olines(run) if "LOUD: missed copy: HANDLE handle [ovl_handle] = 0x0" in ln]
    assert 1 <= len(loud) <= 45, len(loud)      # 40 at most per distinct text, then every 1000th
    assert o.block()["generation"] == gen
    finish(run)


# Issue 0607: the window of the two 0194 tests is a handshake with the reader through the block (overlay_tap.cpp
# TestFrame): it opens at the first test frame >= 40 after the install with no copy in flight and closes once the reader
# has acknowledged a pair and let it go. Before, it was 3 s of wall clock from test frame 40, and a reader started late on
# a loaded machine ran partly after it (copies resumed: accepted 9..91 instead of 1). The waits below are bounds on
# events, not the length of anything; WINDOW_WAIT_S covers a reader start-up on a loaded machine.
WINDOW_WAIT_S = 30.0
_WIN_COUNTS = r"copies (\d+), missed (\d+), generation (\d+)"


def _window(run, o, mode):
    """overlay_test=leader / latequery: ACTIVE, then the window's start line; returns its counters."""
    o.wait_state("ACTIVE")
    ln = run.wait_log(r"overlay_tap: TEST %s window starts at test frame \d+ \(%s\)" % (mode, _WIN_COUNTS),
                      timeout_s=WINDOW_WAIT_S)
    c, m, g = map(int, re.search(_WIN_COUNTS, ln).groups())
    return dict(copiesTotal=c, missedTotal=m, generation=g)


def _window_end(run, mode):
    """The window's end line after the reader let go (never the cap); returns its counters."""
    ln = run.wait_log(r"overlay_tap: TEST %s window ends at test frame \d+: .*\(\d+ ms; %s\)" % (mode, _WIN_COUNTS),
                      timeout_s=WINDOW_WAIT_S)
    assert ": the reader let go (" in ln, ln
    run.wait_log(r"overlay_tap: TEST %s: reader pid \d+ acknowledged generation \d+" % mode, timeout_s=2)
    c, m, g = map(int, re.search(_WIN_COUNTS, ln).groups())
    return dict(copiesTotal=c, missedTotal=m, generation=g)


def test_leader_screen_frames_are_not_stale(run, orun, tmp_path):
    """Issue 0194: overlay_test=leader: a window of Presents that are not map frames (as an AI leader screen, which
    executes no scene-clear list) around the reader's run. The tap makes no copy and counts no missed one; the reader
    binds the last pair, notes the gap once and counts no stale episode (the rule before 0194, which counted Presents,
    counted one). After the window copies resume."""
    o = orun(dict(overlay_test="leader", overlay_trace="1"), seconds=25)
    b0 = _window(run, o, "leader")
    js = tmp_path / "r.json"
    rc, txt = reader(run.section, "--pairs", "0", "--seconds", "1.8", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["stale"] == 0 and r["accepted"] == 1 and r["refused"] == 0, (r, txt)
    assert r["pairs"][0]["generation"] == b0["generation"], (b0, r)
    assert "with 0 map frames since the last copy (frames without the scene-clear list" in txt, txt
    b1 = _window_end(run, "leader")
    assert b1 == b0, (b0, b1)          # no copy, no missed copy, no new generation inside the window
    o.wait(lambda b: b["generation"] > b1["generation"] + 5, "copies after the window", timeout_s=WINDOW_WAIT_S)
    finish(run)
    # the trace: rows of the window are 'N' (not a map frame) with map 0
    tr = Path(run.composite_log).parent / "shell_composite.overlaytap.csv"
    rows = [ln.split(",") for ln in tr.read_text().splitlines() if ln and ln[0].isdigit()]
    assert len(rows) > 100 and sum(1 for x in rows if x[6] == "N" and x[2] == "0") > 30, rows[:5]
    assert any(x[6] == "C" for x in rows)


def test_late_query_is_counted_stale(run, orun, tmp_path):
    """Issue 0194: overlay_test=latequery: a window around the reader's run in which the in-flight copy's event query
    reads as not signalled. Map frames go on and count as missed copies; the reader counts exactly one stale episode in
    map frames (a real lag still counts under the 0194 rule). After the window the query signals and copies resume."""
    o = orun(dict(overlay_test="latequery", overlay_trace="1"), seconds=25)
    b0 = _window(run, o, "latequery")
    js = tmp_path / "r.json"
    rc, txt = reader(run.section, "--pairs", "0", "--seconds", "1.8", "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["stale"] == 1 and r["accepted"] == 1, (r, txt)
    assert r["pairs"][0]["generation"] == b0["generation"], (b0, r)
    assert re.search(r"stale \(lag, counted, D132\): no new copy for \d+ map frames", txt), txt
    b1 = _window_end(run, "latequery")
    # the window's first map frame issues one copy (none was in flight at its start); it is never signalled inside it
    assert b1["generation"] == b0["generation"] and b1["copiesTotal"] <= b0["copiesTotal"] + 1, (b0, b1)
    assert b1["missedTotal"] > b0["missedTotal"] + 30, (b0, b1)
    o.wait(lambda b: b["generation"] > b1["generation"] + 5, "copies after the window", timeout_s=WINDOW_WAIT_S)
    finish(run)
    tr = Path(run.composite_log).parent / "shell_composite.overlaytap.csv"
    rows = [ln.split(",") for ln in tr.read_text().splitlines() if ln and ln[0].isdigit()]
    late = [x for x in rows if x[6] == "M" and x[3] == "0"]
    assert len(late) > 30 and max(int(x[4]) for x in late) > 30, late[-3:]
    # a copy missed while the previous one is in flight is a counted lag: never LOUD, the first 3 logged (MC 20:08)
    assert not [ln for ln in olines(run) if "LOUD" in ln], [ln for ln in olines(run) if "LOUD" in ln][:3]
    assert len([ln for ln in olines(run) if "missed copy (lag, counted, D132)" in ln]) == 3


def test_window_without_a_reader_ends_at_its_cap(run, orun):
    """Ticket 0607, the negative case of the handshake: with no reader the leader window stays open (no copy, no new
    generation, well past the old 3 s) and ends at its 20 s cap, named as such; copies resume after it."""
    o = orun(dict(overlay_test="leader"), seconds=30)
    b0 = _window(run, o, "leader")
    time.sleep(5)
    b = o.block()
    assert b["generation"] == b0["generation"] and b["copiesTotal"] == b0["copiesTotal"], (b0, b)
    assert not [ln for ln in olines(run) if "window ends" in ln]
    ln = run.wait_log(r"overlay_tap: TEST leader window ends at test frame \d+: the cap passed without a reader's "
                      r"hand-back \((\d+) ms; %s\)" % _WIN_COUNTS, timeout_s=WINDOW_WAIT_S)
    assert int(re.search(r"\((\d+) ms;", ln).group(1)) >= 20000, ln
    assert not [x for x in olines(run) if "acknowledged generation" in x]
    o.wait(lambda b: b["generation"] > b0["generation"] + 5, "copies after the window", timeout_s=WINDOW_WAIT_S)
    finish(run)


def test_reader_refuses_a_bad_matrix_and_keeps_the_last_pair(run, orun, tmp_path):
    """overlay_test=nanproj: from test frame 40 OverlayProj holds a NaN. The DLL copies what the EXE has (no check of
    the values); the reader refuses those slots (counted, LOUD once per text) and keeps the last good pair."""
    o = orun(dict(overlay_test="nanproj"), seconds=20)
    o.wait(lambda b: b["generation"] > 80, "80 copies")
    js = tmp_path / "r.json"
    rc, txt = reader(run.section, "--pairs", "0", "--seconds", "2", "--json", str(js))
    r = json.loads(js.read_text())
    assert r["refused"] > 0 and r["state"] == "Active", r
    assert "OverlayProj view 0 element 5 is not finite" in txt
    assert txt.count("LOUD: slot") == 1, txt
    finish(run)


def test_kill_switch_at_start_publishes_off(run, orun):
    o = orun(dict(overlay_tap="0", overlay_test="ok"), seconds=8)
    b = o.wait_state("OFF")
    assert b["copiesTotal"] == 0 and b["failName"] == "NONE"
    run.wait_log(r"overlay_tap: OFF \(overlay_tap=0\)$", timeout_s=3)
    rc, txt = reader(run.section, "--seconds", "1")
    assert rc == 4 and "state Off" in txt, txt
    finish(run)


def test_kill_switch_during_copies(run, orun, tmp_path):
    o = orun(dict(overlay_test="ok"), seconds=20)
    o.wait(lambda b: b["generation"] > 10, "10 copies")
    run.set_ini(overlay_tap="0")
    b = o.wait_state("OFF", timeout_s=5)
    assert b["failName"] == "KILLED"
    run.wait_log(r"overlay_tap: OFF \(overlay_tap=0\): KILLED after \d+ copies; slots and section released", timeout_s=2)
    rc, txt = reader(run.section, "--seconds", "1")
    assert rc == 4 and "state Off" in txt, txt
    run.set_ini(overlay_tap="1")                    # on again: a new start, copies resume
    o.wait(lambda b: b["stateName"] == "ACTIVE" and b["generation"] > 20, "ACTIVE again", timeout_s=8)
    run.wait_log(r"overlay_tap: on again \(overlay_tap=1\)", timeout_s=2)
    finish(run)


def test_map_frame_gate(run, orun):
    """overlay_need_scene=1 (the product's default): the stand-in executes no scene-clear list, so no frame is a map
    frame: no copy, no failure, the state stays NONE."""
    o = orun(dict(overlay_test="ok", overlay_need_scene="1"), seconds=8)
    time.sleep(4)
    b = o.block()
    assert b["stateName"] == "NONE" and b["copiesTotal"] == 0 and b["failName"] == "NONE", b
    finish(run)


# Bug 0120: refusals come at the first Present after the hook commit, which can take seconds under load
SHORT_HOST_S = 20
HOOK_COMMIT_WAIT_S = 18


@pytest.mark.parametrize("build", exe_golden.BUILDS)
def test_real_path_refuses_the_fake_exe(run, build):
    """No overlay_test: the real path. test_host --fake-exe (of either build, ticket 4964) passes the composite's
    validation but has none of the tap's patterns: FAILED PATTERN before any read."""
    main = tapcheck.Mapping(run.section, create=4096)
    try:
        main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
        run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", overlay_tap="1", overlay_test="")
        run.start_host(*HOST, "--fake-exe-build", build, seconds=SHORT_HOST_S)
        run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
        run.wait_log(r"overlay_tap: REFUSED: pattern 'Overlay start-up: .*' at ovl_startup \(VA 0x[0-9A-F]{8}\): "
                     r"(mismatch at byte \d+|unreadable)", timeout_s=10)
        run.wait_log(r"overlay_tap: LOUD: FAILED PATTERN at the install: EXE refused \(byte patterns\)", timeout_s=5)
        b = parse_block(main.read(OVL_OFFSET, OVL_SIZE))
        assert b["stateName"] == "FAILED" and b["failName"] == "PATTERN"
        finish(run)
    finally:
        main.close()


def test_real_path_refuses_an_unvalidated_exe(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", overlay_tap="1", overlay_test="")
    run.start_host("--vsync", "0", "--occluded-sleep", "0", "--load", "after", seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"overlay_tap: REFUSED: the composite did not validate the EXE image", timeout_s=10)
    finish(run)


def test_unknown_test_mode_is_named(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", overlay_tap="1", overlay_test="bogus")
    run.start_host("--vsync", "0", "--occluded-sleep", "0", "--load", "after", seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)
    run.wait_log(r"overlay_tap: overlay_test=bogus is not a known mode - ignored", timeout_s=10)
    finish(run)


# ======================================================================================================================
# static

def test_overlay_signatures_match_exe_on_disk():
    """Ticket 4964: the tap's 8 patterns against both builds' golden EXEs, with each build's table values (exe_golden; a
    missing golden copy or local table fails, never skips)."""
    assert len(exe_golden.rows("shell/composite/overlay_tap.cpp")) == 8
    bad = exe_golden.check_patterns("shell/composite/overlay_tap.cpp")
    assert bad == {b: [] for b in exe_golden.BUILDS}, bad
