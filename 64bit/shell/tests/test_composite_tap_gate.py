"""Ticket 0408: the load gate of shell_composite.dll's height-field tap and water tap, on the stand-in.

On the first load in a process the map view (the camera block's OK) comes up only at the end of the load, long after the
grid became valid; on a reload the camera stays OK through the load screen. So:
- first load (the camera was never OK on a valid grid before, and it comes up after the grid): the copy starts after
  tap_first_delay_frames Presents with the camera OK, without scene-clear frames (the load screen has none);
- reload (a map was shown before in this process) or a camera already OK when the grid became valid: the copy waits for
  tap_delay_frames Presents and tap_scene_frames scene-clear frames, as before 0408;
- tap_first_load=0: the reload rule on every load;
- the water tap follows the same decision (WaterTapEnv::firstLoadOpen).

test_host --fake-exe has its camera OK at the first read, with the grid; camera_test=late (a composite test key) holds
the camera back for camera_test_late reads after each new valid grid, which is what a first load looks like. test_host's
plain path has no scene-clear list (like the load screen); --game has one. Rule 14: the host runs on a hidden desktop
(conftest.py). Research: research/05-graphics-composition/terrain-surface-verification.md 14.
"""
import re
import struct
import time

import pytest

import shellrun
import tapcheck
from test_tap import HOST, TapRun, finish
from test_water_tap import WT_OFFSET, WT_SIZE, parse_block

GAME_HOST = ("--game", "--vary", "1", "--mt", "1") + HOST
LATE = dict(camera_test="late", camera_test_late="20")
RELOAD_GATE = dict(tap_delay_frames="30", tap_scene_frames="30")     # the shipped values of the reload rule


@pytest.fixture
def taprun(run):
    made = []

    def make(ini, seconds=14, host=None):
        t = TapRun(run, ini, seconds, host)
        made.append(t)
        return t
    yield make
    for t in made:
        t.close()


def lines(run, what):
    return [ln for ln in run.log_lines() if what in ln]


def no_tap_block(t):
    return struct.unpack("<II", t.main.read(tapcheck.TAP_OFFSET, 8)) == (0, 0)   # never written: seq 0, state NONE


def started_line(run, load, copy, timeout_s=10):
    return run.wait_log(r"tap: copy %d of load %d started" % (copy, load), timeout_s=timeout_s)


def counts(line):
    ok = int(re.search(r"camera OK for (\d+) Presents", line).group(1))
    scene = int(re.search(r"(\d+) scene-clear frames", line).group(1))
    return ok, scene


# ======================================================================================================================
# the first load

def test_first_load_starts_without_scene_frames(run, taprun):
    """The camera comes up 20 reads after the grid, on the plain host (no scene-clear list, like the load screen): the
    copy starts after tap_first_delay_frames (3) Presents with the camera OK, with 0 scene-clear frames, and is taken."""
    t = taprun(dict(RELOAD_GATE, tap_test="synth", **LATE), seconds=30)
    t.wait_state("READY")
    gate = run.wait_log(r"tap: load gate \(grid seq 1\): ", timeout_s=2)
    assert "FIRST LOAD - no map before in this process and the camera came up after the grid" in gate, gate
    assert int(re.search(r"\((\d+) Presents with the grid valid before\)", gate).group(1)) >= 20, gate
    line = started_line(run, 1, 1, timeout_s=2)
    assert "first-load gate" in line, line
    ok, scene = counts(line)
    assert ok == 3 and scene == 0, line                            # tap_first_delay_frames, no scene frames needed
    log = run.log_lines()
    up = next(i for i, ln in enumerate(log) if re.search(r"camera: status \S+ -> OK", ln))
    assert up < next(i for i, ln in enumerate(log) if "tap: copy 1 of load 1 started" in ln)   # never before the camera
    assert tapcheck.consume(run.section, wait_s=5).code == 0
    t.wait_state("RELEASED")
    finish(run, composite=False)


def test_first_load_waits_for_its_presents(run, taprun):
    """tap_first_delay_frames=40: the first-load copy waits for 40 Presents with the camera OK (not one fewer)."""
    t = taprun(dict(RELOAD_GATE, tap_test="synth", tap_first_delay_frames="40", **LATE), seconds=12)
    t.wait_state("READY")
    ok, scene = counts(started_line(run, 1, 1, timeout_s=2))
    assert ok == 40 and scene == 0
    assert "tap_first_delay_frames=40" in run.wait_log(r"config: ", timeout_s=1)
    finish(run, composite=False)


# ======================================================================================================================
# negative: what keeps the reload rule

def test_camera_ok_with_the_grid_takes_the_reload_rule(run, taprun):
    """Negative: the stand-in's own camera is OK at the first read that sees the grid (as on a reload, where the view
    globals keep the last map's camera): the reload rule, and without a scene-clear list no copy at all."""
    t = taprun(dict(RELOAD_GATE, tap_test="synth"), seconds=6)
    gate = run.wait_log(r"tap: load gate \(grid seq 1\): ", timeout_s=10)
    assert "RELOAD rule - the camera was already OK when the grid became valid" in gate, gate
    time.sleep(3.0)
    assert no_tap_block(t)
    finish(run, composite=False)
    assert not lines(run, "] tap: copy")


def test_first_load_off_keeps_the_scene_gate(run, taprun):
    """Negative: tap_first_load=0 with a late camera: the reload rule (before 0408), so no copy without scene frames."""
    t = taprun(dict(RELOAD_GATE, tap_test="synth", tap_first_load="0", **LATE), seconds=6)
    gate = run.wait_log(r"tap: load gate \(grid seq 1\): ", timeout_s=10)
    assert "RELOAD rule - tap_first_load=0" in gate, gate
    time.sleep(3.0)
    assert no_tap_block(t)
    finish(run, composite=False)
    assert not lines(run, "] tap: copy")


def test_reload_keeps_the_scene_gate(run, taprun):
    """Load 1 is a first load (copy 1 under the 'load screen'); then a new load in the same process (tap_test_load) whose
    camera again comes up only after the grid (as the INCONSISTENT blip of shared-texture-and-compositor.md 9.4): a map
    was shown before, so the reload rule holds and, without scene-clear frames, no copy 2 comes."""
    t = taprun(dict(RELOAD_GATE, tap_test="synth", **LATE), seconds=30)
    t.wait_state("READY", load=1)
    assert tapcheck.consume(run.section, wait_s=5).code == 0
    t.wait_state("RELEASED", load=1)
    run.set_ini(tap_test_load="1")
    gate = run.wait_log(r"tap: load gate \(grid seq 2\): ", timeout_s=10)
    assert "RELOAD rule - a map was shown before in this process" in gate, gate
    time.sleep(4.0)
    assert not lines(run, "] tap: copy 2")
    assert t.tap()["loadSeq"] == 1
    finish(run, composite=False)


def test_reload_copies_after_scene_frames(run, taprun):
    """The same on --game (it has the scene-clear list): load 1 copies at once by the first-load rule; the reload's copy
    2 comes only after 30 Presents with the camera OK and 30 scene-clear frames."""
    t = taprun(dict(RELOAD_GATE, tap_test="synth", **LATE), seconds=45, host=GAME_HOST)
    t.wait_state("READY", load=1)
    assert "first-load gate" in started_line(run, 1, 1, timeout_s=2)
    assert tapcheck.consume(run.section, wait_s=5).code == 0
    t.wait_state("RELEASED", load=1)
    run.set_ini(tap_test_load="1")
    t.wait_state("READY", load=2, copy=2)
    line = started_line(run, 2, 2, timeout_s=2)
    assert "reload gate" in line, line
    ok, scene = counts(line)
    assert ok >= 30 and scene >= 30, line
    assert tapcheck.consume(run.section, wait_s=5).code == 0
    t.wait_state("RELEASED", load=2)
    finish(run, seconds=60, composite=False)


# ======================================================================================================================
# the water tap follows the decision

def water_block(t):
    raw = tapcheck.read_block(t.main, WT_OFFSET, WT_SIZE)
    return parse_block(raw) if raw else {}


def wait_water(t, state, timeout_s=15.0, load=None):
    t_end = time.time() + timeout_s
    b = {}
    while time.time() < t_end:
        b = water_block(t)
        if b and b["state"] == state and (load is None or b["loadSeq"] == load):
            return b
        time.sleep(0.02)
    raise TimeoutError("water state %d not reached: %s; log tail %s" % (state, b, shellrun.tail(t.run.composite_log, 8)))


WATER = dict(water_tap="1", water_test="ok", water_delay_frames="60", water_scene_frames="30", water_ack_timeout_ms="20000")
W_READY = 2


def test_water_first_load_and_reload(run, taprun):
    """water_delay_frames=60 / water_scene_frames=30 on the plain host (no scene-clear list): on the first load (late
    camera) the water copy starts with the height tap's first-load rule; after a new load in the same process it waits
    for its scene frames, so no second copy comes."""
    t = taprun(dict(RELOAD_GATE, **WATER, tap_test="synth", **LATE), seconds=30)
    b1 = wait_water(t, W_READY)
    line = run.wait_log(r"water_tap: load %d: first-load gate" % b1["loadSeq"], timeout_s=2)
    assert re.search(r"camera OK for \d+ Presents, 0 scene-clear frames", line), line
    run.set_ini(tap_test_load="1")
    run.wait_log(r"tap: load gate \(grid seq 2\): RELOAD rule", timeout_s=10)
    time.sleep(4.0)
    assert len(lines(run, "water_tap: copy")) == 1, lines(run, "water_tap:")
    assert not lines(run, "water_tap: load %d: first-load gate" % (b1["loadSeq"] + 1))
    finish(run, composite=False)


def test_water_keeps_its_gate_without_a_first_load(run, taprun):
    """Negative: the stand-in's camera OK with the grid (reload rule): the water tap waits for its scene frames, so on the
    plain host no copy comes, and the tap's off switch does not change the decision (tap=0: the gate still runs)."""
    t = taprun(dict(RELOAD_GATE, **WATER, tap="0"), seconds=6)
    run.wait_log(r"tap: load gate \(grid seq 1\): RELOAD rule", timeout_s=10)
    time.sleep(3.0)
    assert not lines(run, "water_tap: copy")
    finish(run, composite=False)


def test_water_first_load_with_tap_off(run, taprun):
    """tap=0 (the height tap off) with a late camera: the load gate still decides and the water copy starts by the
    first-load rule."""
    t = taprun(dict(RELOAD_GATE, **dict(WATER, water_ack_timeout_ms="2000"), tap="0", **LATE), seconds=10)   # acked by nobody: times out before the host ends
    b = wait_water(t, W_READY)
    run.wait_log(r"water_tap: load %d: first-load gate" % b["loadSeq"], timeout_s=2)
    assert not lines(run, "] tap: copy")
    finish(run, composite=False)
