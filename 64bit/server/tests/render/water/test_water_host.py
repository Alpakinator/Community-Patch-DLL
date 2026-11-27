"""Issue 3607 (epic 3600): the water inside server.exe's render host, end to end on the stand-in: render_host_test.exe
(the render units of server.exe, built by server/tests/render/build_render_tests.py, C5_RENDER_TEST_BUILD) with a fake
camera block, the height-field tap and a fake water tap image, written here (renderfx.py's rig, server/tests/render).

Checked: the server.cfg keys render_water (0 | 1 | placeholder) and render_water_freeze (0 | 1), every other value refused
with its key; render_water=0 draws no water, =placeholder the flat plane only (and still acks the water tap); =1 with
the installed game draws the module's water once the tap delivered and the flat plane before it (logged once, never
LOUD); a stale water image is refused and the plane stays. Needs the installed game (read-only) for the module's art;
ticket 0589: C5_GAME_DIR, else C5R_GAME_DIR, else the setting render_game_dir (server/tests/render/gameinstall.py),
never the Steam install, and only build 4390913; a missing install or another build fails (no skip).

Issue 0369: event-driven, not timed (the block above RUN_BOUND_S): each host run lasts until the frame or the water line
its assertions are about was logged, then the test stops it with render_host_test.exe --stop-event (issue 0333).
"""
import os
import re
import struct
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # renderfx (server/tests/render)
import renderfx as fx   # noqa: E402
import gameinstall   # noqa: E402  (ticket 0589)

GAME, GAME_FROM = gameinstall.game_dir()
WT_OFFSET = 3584
MAGIC_W, HDR, REC = 0x52544157, 120, 128


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


def fnv(data, h=0xCBF29CE484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def water_image(load, pid=4321):
    """A producer-shaped image: a river (object, record, VB 4 x 32 B, IB 6 x R32, draw pass 4), the Water object with
    the D33 clock fields and ocean pass 3, an empty Wave_System object (as under D33)."""
    water = bytearray(0xEC)
    struct.pack_into("<ffffI", water, 0xD8, 0.18, 0.145891, 0.03, 0.00607878, 3)
    blocks = []   # (system, role, index, data, extra)

    def rec(system, role, index=0, stride=0, count=0, fmt=0, topology=0, vb=0xFFFFFFFF, ib=0xFFFFFFFF, vcount=0, icount=0,
            material=0, pas=0):
        return dict(system=system, role=role, index=index, stride=stride, count=count, fmt=fmt, topology=topology, vb=vb, ib=ib,
                    vcount=vcount, icount=icount, material=material, pas=pas)
    vbd = bytes((7 * j + 3) & 0xFF for j in range(4 * 32))
    ibd = b"".join(struct.pack("<I", i % 4) for i in range(6))
    blocks.append((rec(1, 5), bytes(0x30)))
    blocks.append((rec(1, 5, 1), bytes(0x80)))
    blocks.append((rec(1, 1, stride=32, count=4), vbd))
    blocks.append((rec(1, 2, fmt=42, count=6), ibd))
    blocks.append((rec(1, 6, topology=4, vb=2, ib=3, vcount=4, icount=6, material=91, pas=4), b""))
    blocks.append((rec(2, 5), bytes(water)))
    blocks.append((rec(3, 5), bytes(0xB0)))
    off = HDR + REC * len(blocks)
    placed = []
    for r, d in blocks:
        if d:
            off = (off + 15) & ~15
            placed.append((r, d, off))
            off += len(d)
        else:
            placed.append((r, d, 0))
    img = bytearray(off)
    struct.pack_into("<9IfQ", img, 0, MAGIC_W, 1, HDR, REC, len(blocks), load, pid, fx.MAP_W, fx.MAP_H, 10.117, off)
    for i, (r, d, o) in enumerate(placed):
        b = bytearray(REC)
        struct.pack_into("<4IQQQ6I", b, 0, r["system"], r["role"], r["index"], 1 if d else 0, o, len(d), fnv(d) if d else 0,
                         r["stride"], r["count"], r["fmt"], 0, 0, 0)
        struct.pack_into("<6IiIII", b, 64, r["topology"], r["vb"], r["ib"], r["vcount"], r["icount"], 0, 0, 0, r["material"], r["pas"])
        img[HDR + REC * i: HDR + REC * (i + 1)] = b
        if d:
            img[o:o + len(d)] = d
    return bytes(img)


def publish_water(rig, load, copy=1, pid=4321):
    img = water_image(load, pid)
    name = "Local\\VPRenderWater_%d_%s_%d_%d" % (os.getpid(), rig.tag, load, copy)
    rig.taps.append(fx.TapSection(name, img))
    b = bytearray(320)
    struct.pack_into("<8I", b, 0, 2, 2, load, 0, pid, copy, struct.unpack_from("<I", img, 16)[0], 2)
    struct.pack_into("<QQQq", b, 32, len(img), (len(img) + 0xFFFF) & ~0xFFFF, fnv(img), 1)
    nm = name.encode("utf-16-le")
    b[72:72 + len(nm)] = nm
    rig.sec.write(WT_OFFSET, bytes(b))


def water_ack(rig):
    return struct.unpack_from("<IiII", rig.sec.read(WT_OFFSET + 200, 20), 0)   # ackLoadSeq, ackResult, pid, requestSeq


# Issue 0369: no test here asserts on what a fixed time gave. A run used to be 20-25 s of render_host_test.exe; with the
# installed game the fog of war (render_fow=1, the default) holds every frame for its install wait (10 s after the first
# frame of the map: the test host feeds no plot table), then draws on without the fog (no fatal hook in the test host),
# so the first frame the water scene sees comes 10 s plus the start-up after the start, and under MSVC ASan on a loaded
# machine the run ended first (2-4 of 7 failed, 0333). Now each run lasts until the water lines it asserts on were
# logged (event-driven: --stop-event, issue 0333), RUN_BOUND_S is only the upper bound, and WAIT_S the longest wait for
# one event; both are failure bounds, not what a pass depends on.
RUN_BOUND_S = 600
WAIT_S = 240


def water_lines(text):
    """The water scene's per-second lines (srv_water_scene.cpp WaterScene::secondLine: 'water 1s: w_why=NAME w_draws=N
    ...'); a line is written every second whether or not a frame was drawn, and w_why is set when a frame is drawn."""
    return [dict(re.findall(r"(w_\w+)=(\S+)", ln)) for ln in text.splitlines() if "water 1s:" in ln]


def wait_until(rig, p, cond, what, timeout=WAIT_S):
    """Waits until cond(render.log's text) is true and returns the text; fails when the host ended first or the timeout
    passed (both are failures: the run is not cut short by time and then judged)."""
    t_end = time.time() + timeout
    while True:
        ended = not p.alive()   # read before the log: a line written before the exit is seen
        t = rig.text()
        if cond(t):
            return t
        if ended or time.time() >= t_end:
            raise AssertionError("%s: not logged (%s): %s | %s" % (what, "the host ended" if ended else "%d s passed" % timeout,
                                                                    t[-2500:], rig.stdout()[-1000:]))
        time.sleep(0.05)


def stop(rig, p, ev):
    """Stops the host by its stop event, which must be what ended it (not RUN_BOUND_S), and returns render.log's text."""
    ev.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    return rig.text()


def drawn_with(why):
    """A water line whose reason is why. For OFF and PLACEHOLDER this means a frame was drawn: the reason starts as
    NO_TAP (srv_water_scene.h m_why) and changes only when a frame is decided (WaterScene::decide), the first after the
    fog's install wait."""
    assert why in ("OFF", "PLACEHOLDER")
    return lambda t: any(d.get("w_why") == why for d in water_lines(t))


def noted(pattern):
    return lambda t: re.search(pattern, t) is not None


def run_frames(rig, until, what, last=40, **keys):
    """Cameras 1..last, then the run lasts until until(text) is true (the frame the test is about was drawn)."""
    ev, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=RUN_BOUND_S, game_dir=GAME, extra=extra, **keys)
    wait_until(rig, p, lambda t: re.search(r"tap load seq 1 read", t), "the height-field tap read")
    for f in range(2, last + 1):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        time.sleep(0.03)
    wait_until(rig, p, until, what)
    return stop(rig, p, ev)


def no_water_loud(t):
    return [ln for ln in t.splitlines() if "LOUD" in ln and "water" in ln.lower()]


def need_game():
    gameinstall.require_install(GAME)   # ticket 0589: missing, or not build 4390913, fails naming the variables


@pytest.mark.parametrize("key,value", [("render_water", "2"), ("render_water", "full"), ("render_water_freeze", "yes")])
def test_water_keys_refused(rig, key, value):
    p = rig.start(seconds=1, **{key: value})
    assert rig.finish(p) == 2
    assert re.search(r"result: config refused: %s=%s: " % (re.escape(key), re.escape(value)), rig.stdout()), rig.stdout()


def test_render_water_0_draws_no_water(rig):
    need_game()
    publish_water(rig, 1)
    t = run_frames(rig, drawn_with("OFF"), "a frame drawn with w_why=OFF", render_water="0")
    assert "water scene: render_water=0: no water drawn" in t, t[-2000:]
    assert "w_why=OFF" in t and "the water module draws" not in t
    assert water_ack(rig)[:2] == (1, 0)          # the tap is still read and acked
    assert not no_water_loud(t)


def test_render_water_placeholder(rig):
    need_game()
    publish_water(rig, 1)
    t = run_frames(rig, drawn_with("PLACEHOLDER"), "a frame drawn with w_why=PLACEHOLDER", render_water="placeholder")
    assert "the flat placeholder plane only" in t and "w_why=PLACEHOLDER" in t, t[-2000:]
    assert water_ack(rig)[:2] == (1, 0)
    assert not no_water_loud(t)


def test_module_draws_after_the_tap_and_the_plane_before(rig):
    """render_water=1 (default) with the installed game: the plane until the water image arrives (NO_TAP, logged once),
    then the module's water (w_draws > 0), the tap acked 0; no LOUD water line."""
    need_game()
    ev, extra = rig.stop_event()   # 0369: event-driven (was a fixed 25 s run; 3608 had raised it from 7 s for ASan)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=RUN_BOUND_S, game_dir=GAME, extra=extra)
    wait_until(rig, p, noted(r"tap load seq 1 read"), "the height-field tap read")
    for f in range(2, 30):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        time.sleep(0.03)
    wait_until(rig, p, noted(r"water scene: load seq 1: placeholder plane \(NO_TAP"), "a frame drawn with the plane (NO_TAP)")
    publish_water(rig, 1)
    wait_until(rig, p, noted(r"water: tap load seq 1 copy 1 read: 7 records"), "the water tap read")
    for f in range(30, 80):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        time.sleep(0.03)
    # until a second in which the module drew (each camera above is drawn once the fog has decided)
    wait_until(rig, p, lambda t: any(int(d.get("w_draws", 0)) > 0 for d in water_lines(t)), "a water line with w_draws > 0")
    t = stop(rig, p, ev)
    assert "water scene: module ready" in t, t[-3000:]
    assert t.count("placeholder plane (NO_TAP") == 1, t[-3000:]
    assert "load seq 1: the water module draws (meshes 0x2: rivers tapped, waves none" in t, t[-3000:]
    draws = sum(int(x) for x in re.findall(r"w_draws=(\d+)", t))
    assert draws > 0, t[-3000:]
    assert water_ack(rig)[:2] == (1, 0)
    assert re.search(r"water totals: draws [1-9]\d* draw errors 0 ", t), t[-1500:]
    assert not no_water_loud(t)


def test_stale_water_image_keeps_the_plane(rig):
    """A water image of another load than the camera's: refused stale (-3), acked, the plane stays (NO_TAP)."""
    need_game()
    publish_water(rig, 7)
    # 0369: until a frame was drawn with the plane (its note; the per-second w_why starts as NO_TAP before any frame)
    t = run_frames(rig, noted(r"water scene: load seq 1: placeholder plane \(NO_TAP"), "a frame drawn with the plane (NO_TAP)")
    assert water_ack(rig)[:2] == (7, -3), water_ack(rig)
    assert re.search(r"water scene: load seq 1: placeholder plane \(NO_TAP; tap: accepted 0, refused 1, last result -3", t), t[-2000:]
    assert "w_why=NO_TAP" in t and "the water module draws" not in t, t[-2000:]
    assert not no_water_loud(t)
