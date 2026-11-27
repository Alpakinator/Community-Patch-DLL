"""Issue 3301 (epic 3300): the renderer inside server.exe against a fake composite.

The render host test program (render_host_test.exe) runs exactly the render objects server.exe links, from a file of
server.cfg render_* keys, without the DLL. The fake composite is this test (producer half: camera block, tap block and a
C5HF section through renderfx/camblock, the publish block and <base>.Cam for the event pacing) plus fake_consumer.exe
(consumer half: the version-3 keyed-mutex texture and the depth block, read back per frame). Each negative case checks
the outputs (state in <base>.Frame, log lines, the summary) and the exit code. Every process on a hidden desktop.
"""
import importlib.util
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import pytest

import renderfx as fx

REPO = Path(__file__).resolve().parents[3]


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


def cameras(rig, first, last, hz=40.0, publish=False, cam_event=None, **kw):
    for f in range(first, last + 1):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7), **kw))
        if publish:
            rig.frame.publish(f)
            cam_event.set()
        time.sleep(1.0 / hz)


def consumer_opened(c, out, timeout=20.0):
    """Issue 0149: wait until fake_consumer.exe has opened the shared texture (its "opened" line) before the cameras are
    written. Under ASan its start-up can outlast the ~1 s of cameras, and it then took 1 frame of 8 (seen once, 2026-11-13)."""
    t_end = time.time() + timeout
    while time.time() < t_end:
        try:
            if out.exists() and '"opened"' in out.read_text(errors="replace"):
                return
        except OSError:   # an older consumer build holds the file exclusively
            pass
        if not c.alive():
            break
        time.sleep(0.05)
    raise AssertionError("fake_consumer did not open the texture: " + (out.read_text(errors="replace")[-1000:] if out.exists() else "no output"))


# ---------------------------------------------------------------------------------------------------------------
# positive: frames per camera, depth stamp, what the unchanged composite's path reads

def test_frames_per_camera_and_depth_stamp(rig):
    """The version-3 sink publishes one frame per new camera block (legacy pacing: the composite of today publishes no
    publish block), the depth block carries the camera's frameStamp, and the consumer path of shell_composite.dll (keyed
    mutex, frame block, depth texture) reads terrain where the map is and depth 1.0 elsewhere.

    Issue 0413: counted, not timed. The run was a 6 s process, a consumer given 12 s (and 20 s to open the texture) and
    cameras 2..40 at 40 Hz; under ASan on a loaded workstation the consumer had not opened the texture when the process
    ended (0405's whole-file run). Now the run ends through --stop-event (--seconds only bounds it), the consumer gets
    150 s, and each camera is written after the render thread drew the previous one (or skipped it: busy, draw error);
    cameras go on past 40 until the consumer has its 8 frames (research/05-graphics-composition/
    renderer-in-server-design.md 4.19.1)."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=240, render_test_dump=str(rig.dump), render_test_dump_after=1, extra=extra)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    rig.wait_log(r"state (NO_FIELD|-) -> OK|-> OK", timeout=90)
    c, out = rig.consumer(frames=8, timeout=150)
    consumer_opened(c, out, timeout=90)
    last, t_end = 1, time.time() + 120
    while last < 40 or (c.alive() and time.time() < t_end):
        f = last + 1
        s0 = rig.frame.server()
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        _until(rig, p, lambda: (lambda s: s["lastDrawnFrameId"] >= f or s["skippedBusyTotal"] > s0["skippedBusyTotal"]
                                or s["drawErrorsTotal"] > s0["drawErrorsTotal"])(rig.frame.server()), 90, "camera %d drawn" % f)
        last = f
    assert c.wait(30) == 0, out.read_text()[-2000:] if out.exists() else "no consumer output"
    stop.set()
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    t = rig.text()
    assert "pacing: legacy" in t, t[-2000:]
    assert fx.tap_ack(rig.sec)[:2] == (1, 0)
    tot = fx.totals(t)
    assert tot["drawn"] <= tot["camera blocks"] + 3, tot      # never more draws than camera blocks (+ field changes)
    assert tot["drawn"] >= 25, tot
    s, line = fx.summary(rig.stdout())
    assert s["state"] == "STOPPED" and s["exceptions"] == 0 and s["lost devices"] == 0, line
    assert s["published"] == tot["drawn"] == rig.header_frame_counter(), (s, tot, rig.header_frame_counter())
    rows = fx.frames(out)
    assert len(rows) >= 8
    for r in rows:
        assert r["frameCounter"] == r["depthFrameCounter"], r          # colour and depth of one keyed-mutex cycle
        assert 1 <= r["camFrameStamp"] <= last, r                         # the camera the frame was drawn with
        assert r["width"] == fx.W and r["height"] == fx.H
        assert r["drawn"] > 0.3, r
        assert 0 < r["depthDrawnMin"] <= r["depthDrawnMax"] < 1, r
        assert r["depthEmptyMin"] == 1.0 and r["depthEmptyMax"] == 1.0, r
    stamps = [r["camFrameStamp"] for r in rows]
    assert stamps == sorted(stamps), stamps
    # the dump (render_test_dump): terrain in the middle of the view
    a = fx.image(rig.dump)
    drawn = a[..., :3].max(axis=2) > 0
    assert drawn[fx.H // 3:2 * fx.H // 3, fx.W // 3:2 * fx.W // 3].mean() > 0.95, drawn.mean()
    d = np.fromfile(str(rig.dump) + ".depth.f32", dtype="<f4").reshape(fx.H, fx.W)
    assert np.all(d[~drawn] == 1.0) and np.all((d[drawn] > 0) & (d[drawn] < 1))
    # after the orderly stop: the frame section says STOPPED, magic cleared
    srv = rig.frame.server()
    assert srv["state"] == 5 and srv["magic"] == 0, srv
    assert srv["drawsTotal"] == s["published"], (srv, s)
    assert srv["pacing"] == fx.PACE_LEGACY, srv   # issue 3306: the pacing in force is published


def test_static_camera_draws_once(rig):
    """Pacing (D58 6, as shell_server): a camera block that does not change is drawn once (plus the field's change)."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=3)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    tot = fx.totals(rig.text())
    assert 1 <= tot["drawn"] <= 2, tot


@pytest.mark.parametrize("value,flags", [(None, 0), ("0", 0), ("1", 8)])
def test_device_flags_and_submit_counters(rig, value, flags):
    """Ticket 0435: render_prevent_driver_threading=1 creates the device with
    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS (0x8), 0 and the default with flags 0; the device note and
    every `host 1s:` line say which (dev_flags=), and the lines carry the frame submission's time (submit_n, submit_ms,
    submit_max_ms over the frames of that second) and the render thread's CPU time (rt_cpu_ms)."""
    keys = {} if value is None else {"render_prevent_driver_threading": value}
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=6, **keys)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    cameras(rig, 2, 120, hz=40.0)
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert re.search(r"device created, feature level [0-9a-f]+, flags 0x%X \(render_prevent_driver_threading=%d\)"
                     % (flags | fx.ENV_DEVICE_FLAGS, 1 if flags else 0), t), t[-2000:]
    rows = [dict(re.findall(r"(\w+)=(-?[\d.]+)", ln.split(" host 1s: ", 1)[1].split(" | ")[0]))
            for ln in t.splitlines() if " host 1s: " in ln]
    assert rows, t[-2000:]
    assert all(int(r["dev_flags"]) == flags | fx.ENV_DEVICE_FLAGS for r in rows), rows
    drew = [r for r in rows if int(r["submit_n"]) > 0]
    assert drew, rows
    for r in drew:
        assert 0 < float(r["submit_ms"]) <= float(r["submit_max_ms"]), r
    assert all(float(r["rt_cpu_ms"]) >= 0 for r in rows), rows
    assert sum(float(r["rt_cpu_ms"]) for r in rows) > 0, rows
    assert sum(int(r["submit_n"]) for r in rows) <= fx.totals(t)["drawn"], (rows, fx.totals(t))


@pytest.mark.parametrize("value,flags", [(None, 8), ("0", 0), ("1", 8)])
def test_recording_draws_with_the_flag_unless_told(rig, tmp_path, value, flags):
    """Ticket 0435: with render_test_record the device gets the flag unless render_prevent_driver_threading is given
    (an explicit 0 wins), and scene.json's render_config names the live device's flag."""
    keys = {} if value is None else {"render_prevent_driver_threading": value}
    bundle = tmp_path / "bundles" / "SYNTH-0435_20261117-000000_test"
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=6, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_test_record_dump="all", **keys)
    c, out = rig.consumer(frames=30)
    consumer_opened(c, out, timeout=90)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    cameras(rig, 2, 120, hz=40.0)
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert re.search(r"device created, feature level [0-9a-f]+, flags 0x%X \(render_prevent_driver_threading=%d\)"
                     % (flags | fx.ENV_DEVICE_FLAGS, 1 if flags else 0), t), t[-2000:]
    assert re.search(r"recorder: bundle '.*' finished: 2 frames", t), t[-3000:]
    m = json.loads((bundle / "scene.json").read_text(encoding="utf-8"))
    assert m["render_config"]["render_prevent_driver_threading"] == ("1" if flags else "0"), m["render_config"]


def test_event_pacing_with_publish_block(rig):
    """Once the composite writes the publish block (vp_frame.h step 1) the render thread is woken by <base>.Cam instead
    of the 0.5 ms poll, and draws the published cameras."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=5)
    rig.wait_log(r"tap load seq 1 read")
    cameras(rig, 1, 60, hz=50, publish=True, cam_event=cam)
    assert rig.frame.server()["pacing"] == fx.PACE_EVENT, rig.frame.server()   # issue 3306: published in the server block
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "pacing: the composite publishes: woken by <base>.Cam" in t, t[-2000:]
    tot = fx.totals(t)
    assert tot["drawn"] >= 40, tot
    m = re.findall(r"waits_event=(\d+) waits_legacy=(\d+)", t)
    assert m and int(m[-1][0]) > 0, m[-3:]
    s, line = fx.summary(rig.stdout())
    assert s["pace fallbacks"] == 0 and s["resumes"] == 0, line   # issue 3312: a composite that publishes never falls back


# ---------------------------------------------------------------------------------------------------------------
# ticket 0713: the prepare phase (the composite's <base>.Tap after the last tap of its Present hook)

def _frames_with_taps(rig, first, last, cam, tap=None, tap_number=True, hz=50):
    """As the composite with 0713: per frame F the camera block (frameStamp F, presentCount F - 1) and the publish block,
    <base>.Cam, then (the EXE's frame runs on; the insertion, the 3D lists) the Present hook's taps: tapPresent = F, then
    <base>.Tap (tap=None: the number written without the event, as when the event is lost)."""
    for f in range(first, last + 1):
        rig.sec.write_camera(fx.synth_camera(frame=f, present=f - 1, pan=float(f % 7)))
        rig.frame.publish(f)
        cam.set()
        time.sleep(0.5 / hz)
        if tap_number:
            rig.frame.tap_present(f)
        if tap is not None:
            tap.set()
        time.sleep(0.5 / hz)


def _prepare_lines(text):
    return [dict((k, int(v)) for k, v in re.findall(r"(\w+)=(-?\d+)", ln.split("prepare 1s:")[1])) for ln in text.splitlines()
            if "prepare 1s:" in ln]


def _stage_mean(text, stage):
    """The `pass 1s:` lines' per-second mean of one stage (ms), summed over the run's seconds."""
    tot = 0.0
    for ln in text.splitlines():
        if " pass 1s: " in ln and "| max/mean ms:" in ln:
            m = re.search(r" %s=(-?[\d.]+)/(-?[\d.]+)" % stage, ln.split("| max/mean ms:")[1])
            if m:
                tot += float(m.group(2))
    return tot


def test_prepare_phase_cameras_go_straight_to_decide(rig):
    """With the tap event the drain and the polls run in a pass of their own (stage `prepare`), and a camera whose tap number
    was prepared goes straight to decide: nearly every camera is a hit, every camera is still drawn."""
    cam = fx.Event(rig.section + ".Cam")
    tap = fx.Event(rig.section + ".Tap")
    rig.events += [cam, tap]
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=6)
    rig.wait_log(r"tap load seq 1 read")
    rig.wait_log(r"prepare: the feed's drain and the taps' polls run on <base>.Tap")
    d0 = rig.frame.server()["drawsTotal"]
    _frames_with_taps(rig, 2, 101, cam, tap)
    time.sleep(0.15)
    drawn = rig.frame.server()["drawsTotal"] - d0
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    s, line = fx.summary(rig.stdout())
    assert 75 <= drawn <= 102, (drawn, line)
    assert s["prepare passes"] >= 60 and s["camera hits"] >= 60, line
    assert s["camera hits"] >= 3 * s["camera polls"], line
    assert s["hold repolls"] == 0 and s["exceptions"] == 0, line
    pl = _prepare_lines(t)
    assert pl and pl[-1]["on"] == 1 and pl[-1]["tap_event"] == 1 and pl[-1]["cam_hits"] > 0, pl[-3:]
    assert _stage_mean(t, "prepare") > 0.0, [ln for ln in t.splitlines() if "pass 1s:" in ln][-2:]


def test_prepare_phase_off_polls_at_every_camera(rig):
    """render_prepare=0 (the A/B arm): the event is not opened, no pass prepares, every pass drains and polls as before."""
    cam = fx.Event(rig.section + ".Cam")
    tap = fx.Event(rig.section + ".Tap")
    rig.events += [cam, tap]
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=5, render_prepare=0)
    rig.wait_log(r"tap load seq 1 read")
    d0 = rig.frame.server()["drawsTotal"]
    _frames_with_taps(rig, 2, 61, cam, tap)
    time.sleep(0.15)
    drawn = rig.frame.server()["drawsTotal"] - d0
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    s, line = fx.summary(rig.stdout())
    assert 45 <= drawn <= 62, (drawn, line)
    assert s["prepare passes"] == 0 and s["camera hits"] == 0 and s["camera polls"] == 0, line
    assert "prepare: off (render_prepare=0)" in t, t[-2000:]
    assert "prepare 1s:" not in t
    assert _stage_mean(t, "prepare") == 0.0


# ---- ticket 0753 (J3): record ahead (render_record_ahead; SceneOptions::recordAhead) ----

def _record_ahead_lines(t):
    return [dict((k, float(v)) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", ln.split("record ahead 1s: ", 1)[1].split(" | ")[0]))
            for ln in t.splitlines() if "record ahead 1s: " in ln]


def test_record_ahead_always_records_and_executes_every_draw(rig):
    """render_record_ahead=1 with the TEST key render_test_record_ahead_always=1 (this host has no units, so no job is in
    flight): every drawn frame's end is recorded into a command list and executed at its place; the frames are drawn and
    published as without it, and nothing is dropped or refused."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=6, render_record_ahead=1, render_test_record_ahead_always=1)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    cameras(rig, 2, 80, hz=40.0)
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert re.search(r"record ahead: on: .*\(render_record_ahead=1, ticket 0753\)", t), t[-2000:]
    assert "TEST KEY render_test_record_ahead_always=1" in t, t[-2000:]
    rows = _record_ahead_lines(t)
    assert rows and sum(r["lists"] for r in rows) > 0, rows
    m = re.search(r"record ahead totals: lists (\d+) not forked (\d+) finish failed (\d+) dropped (\d+)", t)
    assert m, t[-2000:]
    lists, _, failed, dropped = (int(x) for x in m.groups())
    assert lists > 0 and failed == 0 and dropped == 0, m.group(0)
    assert lists <= fx.totals(t)["drawn"], (lists, fx.totals(t))
    # step 2: no unit scene here, so the shadow step hands the terrain the tapped map unchanged: always predicted
    m2 = re.search(r"terrain lists (\d+) mid lists (\d+) no prediction (\d+) mispredicted (\d+)", t)
    assert m2, t[-2000:]
    terrain, mid, nopred, miss = (int(x) for x in m2.groups())
    # (a frame whose terrain draw fails ends at the terrain: its terrain list has no middle or end list)
    assert terrain >= lists and mid == lists and nopred == 0 and miss == 0, (m.group(0), m2.group(0))


def test_record_ahead_end_only_records_no_terrain_list(rig):
    """render_test_record_ahead_end_only=1 (TEST, the A/B of the two steps): only the draw stage's end is recorded."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=5, render_record_ahead=1, render_test_record_ahead_always=1, render_test_record_ahead_end_only=1)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    cameras(rig, 2, 40, hz=40.0)
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "on, the end only (TEST KEY render_test_record_ahead_end_only=1)" in t, t[-2000:]
    m = re.search(r"record ahead totals: lists (\d+) .* terrain lists (\d+) mid lists (\d+)", t)
    assert m and int(m.group(1)) > 0 and int(m.group(2)) == 0 and int(m.group(3)) == 0, t[-2000:]


@pytest.mark.parametrize("keys", [{"render_record_ahead": "0"}, {"render_record_ahead": "auto", "render_frame_jobs": "0"}])
def test_record_ahead_off_and_auto_without_jobs(rig, keys):
    """render_record_ahead=0: off, no line of its own; auto with render_frame_jobs=0: off too (A4: only with workers)."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=3, **keys)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert re.search(r"record ahead: off \(render_record_ahead=%s, ticket 0753\)" % keys["render_record_ahead"], t), t[-2000:]
    assert "record ahead 1s:" not in t, keys


@pytest.mark.parametrize("key,value", [("render_record_ahead", "2"), ("render_record_ahead", "on"),
                                       ("render_record_ahead", "Auto"), ("render_test_d3d_debug", "yes"),
                                       ("render_test_record_ahead_always", "2"), ("render_test_record_ahead_end_only", "on")])
def test_record_ahead_keys_refused(rig, key, value):
    """Negative: render_record_ahead takes auto, 0 or 1, the two TEST keys 0 or 1; anything else refuses the configuration
    (exit 2, the host's result line names the key)."""
    p = rig.start(seconds=1, **{key: value})
    assert rig.finish(p) == 2
    assert "result: config refused: %s=%s: " % (key, value) in rig.stdout(), rig.stdout()


def test_prepare_phase_a_newer_tap_polls_at_the_camera(rig):
    """A tap number written with no <base>.Tap (the event lost, or the camera came first): the camera sees a number it did not
    prepare and drains and polls first, as before 0713; no pass prepares. A camera may still go straight to decide when the
    pass of the camera before ran after this frame's tap number was written (its polls saw it): rare, and only on a slow host
    (one of 60 under ASan, 2026-11-24)."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=5)
    rig.wait_log(r"tap load seq 1 read")
    rig.wait_log(r"prepare: the feed's drain and the taps' polls run on <base>.Tap")
    d0 = rig.frame.server()["drawsTotal"]
    _frames_with_taps(rig, 2, 61, cam, None)
    time.sleep(0.15)
    drawn = rig.frame.server()["drawsTotal"] - d0
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    s, line = fx.summary(rig.stdout())
    assert 45 <= drawn <= 62, (drawn, line)
    assert s["prepare passes"] == 0 and s["camera polls"] >= 45 and s["camera hits"] <= 0.1 * s["camera polls"], line


def test_prepare_phase_without_a_tap_number_is_todays_loop(rig):
    """A composite before 0713 (no tap number, no event): every camera drains and polls, as before."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=5)
    rig.wait_log(r"tap load seq 1 read")
    d0 = rig.frame.server()["drawsTotal"]
    _frames_with_taps(rig, 2, 61, cam, None, tap_number=False)
    time.sleep(0.15)
    drawn = rig.frame.server()["drawsTotal"] - d0
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    s, line = fx.summary(rig.stdout())
    assert 45 <= drawn <= 62, (drawn, line)
    assert s["camera hits"] == 0 and s["prepare passes"] == 0 and s["camera polls"] >= 45, line


# ---------------------------------------------------------------------------------------------------------------
# issue 3312: the composite stops publishing while it goes on presenting (version 3 without lockstep, ring or depth)

def _phase(rig, first, last, publish, cam, hz=50):
    """Camera blocks first..last as the composite writes them; with publish the publish block and <base>.Cam as well
    (frameStamp F, presentCount F - 1 before that frame's Present), else as version 3 (frameStamp = presentCount = F).
    Returns (cameras written, draws by the server meanwhile)."""
    d0 = rig.frame.server()["drawsTotal"]
    for f in range(first, last + 1):
        rig.sec.write_camera(fx.synth_camera(frame=f, present=f - 1 if publish else f, pan=float(f % 7)))
        if publish:
            rig.frame.publish(f)
            cam.set()
        time.sleep(1.0 / hz)
    time.sleep(0.15)   # the last camera's draw
    return last - first + 1, rig.frame.server()["drawsTotal"] - d0


def _second_lines(text):
    return [dict((k, int(v)) for k, v in re.findall(r"(\w+)=(-?\d+)", ln.split("|")[0])) for ln in text.splitlines()
            if "host 1s:" in ln]


def test_pacing_falls_back_when_publishing_stops_and_returns_when_it_resumes(rig):
    """Event pacing once the publish block's seq is non-zero; when the composite stops publishing while its Presents go on
    (presentCount advances, no <base>.Cam), the render thread polls the camera block again and draws every camera (before
    3312: one draw per 100 ms wait timeout, ~10 a second); the next publish returns it to the events. Draw rate checked in
    all three phases, both switches counted in the per-second line and the summary."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=14)
    rig.wait_log(r"tap load seq 1 read")
    rig.wait_log(r"-> OK")
    # issue 0149: the render thread logs the legacy note at its first pacing decision, after the first draw; a publish
    # before that (the first draw is slow under ASan) left the note out (count 0, 1 run in 7). Wait for it.
    rig.wait_log(r"pacing: legacy \(the composite has never written the publish block\)")
    n_a, d_a = _phase(rig, 2, 81, True, cam)
    srv_a = rig.frame.server()
    n_b, d_b = _phase(rig, 82, 161, False, cam)
    srv_b = rig.frame.server()
    n_c, d_c = _phase(rig, 162, 241, True, cam)
    srv_c = rig.frame.server()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    rates = {"publishing": (n_a, d_a), "stopped": (n_b, d_b), "resumed": (n_c, d_c)}
    # every phase draws (nearly) every camera; before the fix the stopped phase drew ~10 a second (~20 of 80 here)
    for k, (n, d) in rates.items():
        assert 0.75 * n <= d <= n + 2, (k, rates)
    assert srv_a["pacing"] == fx.PACE_EVENT and srv_b["pacing"] == fx.PACE_LEGACY and srv_c["pacing"] == fx.PACE_EVENT, (srv_a, srv_b, srv_c)
    assert "pacing: the composite publishes: woken by <base>.Cam" in t
    m = re.search(r"pacing: (\d+) Presents without a publish \(presentCount (\d+) -> (\d+), seq \d+\): the camera block is polled", t)
    assert m and int(m.group(1)) > 3, t[-3000:]
    assert re.search(r"pacing: the composite publishes again \(seq \d+, presentCount \d+\): woken by <base>.Cam \(resume 1\)", t), t[-3000:]
    # the legacy note of a composite that never published is given once (before the first publish), not for the fallback
    assert t.count("pacing: legacy (the composite has never written the publish block)") == 1, t[-3000:]
    s, line = fx.summary(rig.stdout())
    assert s["pace fallbacks"] == 1 and s["resumes"] == 1, line
    assert s["exceptions"] == 0 and s["state"] == "STOPPED", line
    sec = _second_lines(t)
    assert sec and sec[-1]["pace_fallbacks"] == 1 and sec[-1]["pace_resumes"] == 1 and sec[-1]["pace_fallback"] == 0, sec[-1]
    assert any(x["pace_fallback"] == 1 for x in sec), [x["pace_fallback"] for x in sec]
    # the fallback polled the camera block (0.5 ms waits, counted as legacy waits and as fallback waits)
    assert sec[-1]["waits_fallback"] > 200 and sec[-1]["waits_legacy"] >= sec[-1]["waits_fallback"], sec[-1]


def test_pacing_tolerates_three_presents_without_a_publish_falls_back_at_four(rig):
    """The boundary of the rule: presentCount 3 ahead of the last publish (a read between the two writes, a frame whose
    camera could not be read) keeps the events; 4 ahead falls back."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=6)
    rig.wait_log(r"tap load seq 1 read")
    _phase(rig, 2, 11, True, cam)
    # the last publish (frame 11) left presentCount 10; 3 Presents without a publish (11..13), then 0.35 s: several 100 ms
    # timeouts look at it
    for f in range(12, 15):
        rig.sec.write_camera(fx.synth_camera(frame=f, present=f - 1))
    time.sleep(0.35)
    srv3 = rig.frame.server()
    _phase(rig, 15, 20, True, cam)
    # now 4 (presentCount 19 -> 20..23)
    for f in range(21, 25):
        rig.sec.write_camera(fx.synth_camera(frame=f, present=f - 1))
    time.sleep(0.35)
    srv4 = rig.frame.server()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert srv3["pacing"] == fx.PACE_EVENT and srv4["pacing"] == fx.PACE_LEGACY, (srv3, srv4)
    s, line = fx.summary(rig.stdout())
    assert s["pace fallbacks"] == 1 and s["resumes"] == 0, line
    assert re.search(r"pacing: 4 Presents without a publish", rig.text()), rig.text()[-3000:]


def test_pacing_stays_on_events_when_presents_stop_too(rig):
    """Negative: a composite that neither publishes nor presents (minimised, a load without frames) leaves presentCount
    where it was: the render thread keeps the events and their 100 ms timeout, no 0.5 ms poll.

    Issue 0333: counted, not timed. The check was "10 wait timeouts by the end of a 4 s run" (1.5 s of silence); under
    MSVC ASan on a loaded workstation the run ended with fewer. The silence now lasts until two per-second lines written
    during it are 10 timeouts apart (the run ends through --stop-event; --seconds only bounds it), and between those lines
    every wait is an event wait: the legacy count does not move, no fallback wait, no pacing switch."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=90, extra=extra)
    rig.wait_log(r"tap load seq 1 read")
    _phase(rig, 2, 31, True, cam)
    t0 = rig.text()
    quiet_from = t0.rfind("\n") + 1   # whole per-second lines after this point are written in the silence

    def quiet():
        return _second_lines(rig.text()[quiet_from:])

    rig.wait_second_line(lambda d: len(quiet()) >= 1, timeout=60, p=p)
    base = quiet()[0]
    # ends early when a wait of another kind is counted (the checks below then name it)
    rig.wait_second_line(lambda d: d["wait_timeouts"] >= base["wait_timeouts"] + 10 or d["waits_legacy"] != base["waits_legacy"]
                         or d["waits_fallback"] > 0, timeout=60, p=p)
    srv = rig.frame.server()
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    assert srv["pacing"] == fx.PACE_EVENT, srv
    s, line = fx.summary(rig.stdout())
    assert s["pace fallbacks"] == 0, line
    q = quiet()
    end = next((i for i, d in enumerate(q) if d["wait_timeouts"] >= base["wait_timeouts"] + 10), len(q) - 1)
    for d in q[:end + 1]:
        assert d["waits_legacy"] == base["waits_legacy"] and d["waits_fallback"] == 0 and d["pace_fallback"] == 0, (base, d)
    assert end >= 1 and q[end]["wait_timeouts"] >= base["wait_timeouts"] + 10, (base, q)


# ---------------------------------------------------------------------------------------------------------------
# ticket 0414: no pacing wait while bakes are pending and the pass's slice baked (render_bake_no_sleep, default 1)

NS_MAP = 16           # plots a side: 256 land tiles for the stand-in baker
NS_AWAY = 50000.0     # camera x far off the map: no tile visible, every bake is an idle() one (test_bake_yield.py)


def _residency_last(text):
    rows = [ln for ln in text.splitlines() if "| residency land=" in ln]
    assert rows, text[-2000:]
    return {k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", rows[-1].split("| residency ", 1)[1].split(" | ")[0])}


def _bake_ahead_run(rig, **keys):
    """The whole-map bake-ahead of a 16 x 16 field by the stand-in baker, one bake per idle() slice (a budget of 0.01 ms
    ends the slice after its first bake; rate 100000/s: never short of tokens), under event pacing (the publish block
    written before the start, presentCount still: the composite's events, no 0.5 ms poll, no <base>.Cam after the first),
    so that a pass that waits waits its 2 ms timeout. Ends once the bake-ahead is complete and a host 1s line has
    followed it. Returns (the last residency counters, the last host 1s line, render.log)."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    stop, extra = rig.stop_event()
    samples = (NS_MAP * 64, NS_MAP * 64)
    rig.sec.write_camera(fx.synth_camera(frame=1, present=0, samples=samples, pan=NS_AWAY))
    rig.frame.publish(1)
    cam.set()
    rig.tap(load_seq=1, payload=fx.c5hf(fx.synth_field(NS_MAP, NS_MAP), NS_MAP, NS_MAP), map_w=NS_MAP, map_h=NS_MAP)
    p = rig.start(seconds=120, extra=extra, render_tiles="bake", render_baker="standin", render_bake_budget_ms=0.01,
                  render_bake_rate=100000, **keys)
    rig.wait_log(r"residency: bake-ahead complete", timeout=90)
    n0 = len(fx.second_lines(rig.text()))
    rig.wait_second_line(lambda d: len(fx.second_lines(rig.text())) > n0 + 1, timeout=30, p=p)
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "pacing: the composite publishes: woken by <base>.Cam" in t, t[-2000:]
    # event pacing from the first pass on: never the 0.5 ms legacy poll or the idle pace
    assert "pacing: legacy" not in t and "pacing: no camera to draw" not in t, t[:3000]
    res = _residency_last(t)
    assert res["land"] == NS_MAP * NS_MAP and res["ahead_done"] == 1 and res["failed"] == 0, res
    host = fx.second_lines(t)[-1]
    s, line = fx.summary(rig.stdout())
    assert s["exceptions"] == 0 and s["state"] == "STOPPED", line
    print("0414 %s: ahead_ms %s bakes %s idle_slices %s | idle_no_sleep %s bake_checks %s idle_calls %s waits_event %s"
          % (keys or "default", res["ahead_ms"], res["bakes"], res["idle_slices"], host["idle_no_sleep"],
             host["bake_checks"], host["idle_calls"], host["waits_event"]))
    return res, host, t


def test_no_pacing_wait_while_the_slice_bakes(rig, tmp_path, hidden_desktop, built, exe):
    """Ticket 0414: with bakes pending, a pass whose idle() slice baked goes on to the next pass without the 2 ms pacing
    wait (counted in idle_no_sleep, noted once); render_bake_no_sleep=0 keeps the wait of 3308 after every pass. Counted,
    and one bound in time that a loaded or sanitized machine cannot break: every pass of the second run waits its 2 ms
    timeout (no <base>.Cam is set during the bake-ahead), at least 1 ms on the 1 ms timer, so it takes at least 0.5 ms per
    bake longer than the first, whose bakes cost the same."""
    res, host, t = _bake_ahead_run(rig)
    assert host["bake_no_sleep"] == 1, host
    assert "pacing: bakes pending: no pacing wait after a pass whose slice baked (render_bake_no_sleep=1, ticket 0414)" in t
    # one bake per slice; every slice of the bake-ahead skipped the wait (pending() is cached for 100 ms: its end may
    # still see the last slices as pending, never the other way round once the field is in)
    assert res["bakes"] >= NS_MAP * NS_MAP and res["idle_slices"] >= NS_MAP * NS_MAP, res
    assert res["idle_slices"] - 2 <= host["idle_no_sleep"] <= res["idle_slices"], (res, host)
    assert host["bake_checks"] >= res["idle_slices"], (res, host)   # a check before each bake, one at each slice's end
    # the old wait, the same run otherwise (a second rig: its own section, log and process)
    rig2 = fx.Rig(tmp_path / "sleep", hidden_desktop, built, exe, "nosleep0")
    (tmp_path / "sleep").mkdir()
    try:
        res2, host2, t2 = _bake_ahead_run(rig2, render_bake_no_sleep=0)
    finally:
        rig2.close()
    assert host2["bake_no_sleep"] == 0 and host2["idle_no_sleep"] == 0, host2
    assert "no pacing wait after a pass" not in t2
    assert res2["bakes"] == res["bakes"] and res2["idle_slices"] >= NS_MAP * NS_MAP, (res, res2)
    assert res2["ahead_ms"] >= res["ahead_ms"] + 0.5 * res2["bakes"], (res["ahead_ms"], res2["ahead_ms"], res2["bakes"])


def test_no_sleep_needs_the_yield_check(rig):
    """Negative (ticket 0414): the slice's bakes are seen through the residency's yield check; with render_bake_yield=0
    there is none, so the loop keeps the 2 ms wait after every pass although render_bake_no_sleep=1."""
    res, host, t = _bake_ahead_run(rig, render_bake_yield=0)
    assert res["yield"] == 0, res
    assert host["bake_no_sleep"] == 1 and host["idle_no_sleep"] == 0 and host["bake_checks"] == 0, host
    assert "no pacing wait after a pass" not in t


def test_feed_drained_every_iteration(rig):
    """The event feed is the renderer's only game input: events pushed on another thread (a DLL thread's stand-in) are
    drained by the render thread and counted."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    p = rig.start(seconds=3, feed_events=500)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    s, line = fx.summary(rig.stdout())
    assert s["events"] == 500 and s["feed drains"] >= 1, line


def test_feed_changed_plots_reach_the_tile_source(rig):
    """Issue 3306 item 3: plots whose record changed after the snapshot (the feed's Drain changedPlots: here 300
    improvement patches, as GameplayPlotStateChange makes them) reach the scene's tile-cache source
    (TileCacheSource::onPlotsChanged); the EXE-cache tap counts them (the EXE re-bakes, the tap follows), none is lost."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=4, feed_plots=300)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert "plots patched 300" in rig.stdout(), rig.stdout()[-1500:]
    # negative: the same value again is no change and reaches nobody (the count below stays 300)
    assert "repeated patch of plot 0 changed nothing" in rig.stdout(), rig.stdout()[-1500:]
    tot = fx.totals(rig.text())
    assert tot["source plots changed"] == 300 and tot["lost plots changed"] == 0, tot
    s, line = fx.summary(rig.stdout())
    assert s["changed plots"] == 300, line
    assert re.search(r"plots_changed=300", rig.text()), "the per-second line of the source"


def test_feed_off_nothing_reaches_the_source(rig):
    """Negative (issue 3306 item 3): with the feed off (render_host_test turns it on only for --feed-*, as server.exe
    only for render=1 / feed=1) nothing reaches the source and nothing is counted lost."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=3)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    tot = fx.totals(rig.text())
    assert tot["source plots changed"] == 0 and tot["lost plots changed"] == 0, tot


def test_resize_request(rig):
    """The composite's resize request (reqSeq): a new texture generation at the requested size, frames at that size.
    Ticket 0607: the host runs until its stop event (was --seconds 6), the test waits for the request's log line before
    any camera, and cameras go on until the consumer has its 4 frames (was a fixed 39 at 40 Hz: on a loaded machine the
    consumer took 3 of 4 before they ended, or the host's 6 s ended first)."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until()
    rig.wait_log(r"-> OK", p=p)
    c, out = rig.consumer(frames=4, timeout=fx.WAIT_S, request=(400, 300))
    consumer_opened(c, out, timeout=fx.WAIT_S)
    rig.wait_log(r"client requested 400x300", p=p)
    f, t_end = 2, time.time() + fx.WAIT_S
    while c.alive() and time.time() < t_end:
        cameras(rig, f, f + 9)
        f += 10
    assert c.wait(fx.WAIT_S) == 0, out.read_text(errors="replace")[-1500:]
    assert rig.stop(p) == 0
    assert re.search(r"client requested 400x300", rig.text()), rig.text()[-1500:]
    rows = fx.frames(out)
    assert rows[-1]["width"] == 400 and rows[-1]["height"] == 300 and rows[-1]["generation"] >= 2, rows[-1]


# ---------------------------------------------------------------------------------------------------------------
# the failure policy: device lost, exceptions; the process goes on

def _until(rig, p, pred, timeout, what):
    """Issue 0405: polls pred() until it is true; fails when the timeout passes or render_host_test.exe ended first."""
    t_end = time.time() + timeout
    while True:
        ended = not p.alive()   # read before pred: a state written before the exit is seen
        v = pred()
        if v:
            return v
        if ended or time.time() >= t_end:
            raise AssertionError("%s: not reached in %.0f s (process %s): server %s; log: %s" % (
                what, timeout, "ended" if ended else "alive", rig.frame.server(), rig.text()[-2000:]))
        time.sleep(0.02)


def test_device_lost_rebuilt_in_process(rig):
    """A lost device (forced by render_test_device_lost) is rebuilt in the process: back-off, a new device, the kept
    field re-applied, drawing goes on; deviceLostTotal counts it; the state is READY again.

    Issue 0405: counted, not timed. The run was "cameras 2..7 at 20 Hz, then 10 s for the rebuild, inside an 8 s process";
    a render thread that fell behind for 0.3 s drew the newest camera only (3 draws, the loss at draw 6 never reached) and
    an ASan start-up could eat the 8 s. Now each camera is written after the previous one was drawn, so draw n is camera
    n; the run ends through --stop-event and --seconds only bounds it (research/05-graphics-composition/
    renderer-in-server-design.md 4.19)."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=240, render_test_device_lost=6, render_rebuild_backoff_ms=50, extra=extra)
    rig.wait_log(r"-> OK", timeout=90)
    # draws 1..5 publish cameras 1..5; draw 6 (camera 6) is the forced loss; the next loss would be draw 12
    for f in range(1, 7):
        if rig.frame.server()["deviceLostTotal"] >= 1:
            break
        if f > 1:
            rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        _until(rig, p, lambda: rig.frame.server()["lastDrawnFrameId"] >= f or rig.frame.server()["deviceLostTotal"] >= 1,
               90, "camera %d drawn" % f)
    _until(rig, p, lambda: "rebuild: device and renderer created again" in rig.text(), 90, "the rebuild")
    srv = _until(rig, p, lambda: (lambda s: s if s["state"] == 2 else None)(rig.frame.server()), 30, "state READY again")
    assert srv["deviceLostTotal"] == 1, srv
    for f in (10, 11, 12):               # draws 7..9 (the rebuilt scene may add one): below the next loss at draw 12
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        _until(rig, p, lambda: rig.frame.server()["lastDrawnFrameId"] >= f, 90, "camera %d drawn after the rebuild" % f)
    srv = rig.frame.server()
    stop.set()
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    assert srv["state"] == 2 and srv["deviceLostTotal"] == 1, srv
    t = rig.text()
    assert "re-applying the kept field" in t and "LOUD: device lost" in t, t[-2000:]
    s, line = fx.summary(rig.stdout())
    assert s["lost devices"] == 1 and s["rebuilds"] == 1 and s["state"] == "STOPPED", line
    assert s["published"] >= 8, line     # cameras 1..5 and 10..12


def test_three_lost_devices_fail_the_renderer_not_the_process(rig):
    """The third lost device inside 5 minutes: VPFS_FAILED (VPFSF_REBUILDS); the process goes on and exits 0.

    Issue 0405: the run was bounded by a 6 s process and 2.8 s of cameras; under ASan on a loaded machine the third
    rebuild was not reached in time (0382's hand-back). Now a new camera every 0.1 s until FAILED (at most 120 s), the
    run ends through --stop-event.

    Issue 0445: Fail() writes the "renderer FAILED" line before SetState puts VPFS_FAILED into the frame block
    (srv_render_host.cpp), so a test that read the block once, right after the line, could see the state before it
    (READY). The block is now waited on for FAILED (as device_lost waits for READY), then the line; the assertions are
    the same (research/05-graphics-composition/renderer-in-server-design.md 4.19.2)."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=240, render_test_device_lost=1, render_rebuild_backoff_ms=20, extra=extra)
    rig.wait_log(r"-> OK", timeout=90)
    t_end = time.time() + 120
    f = 2
    while rig.frame.server()["state"] != 4 and "renderer FAILED" not in rig.text():
        assert p.alive() and time.time() < t_end, "no FAILED after %d cameras: server %s; log: %s" % (
            f - 1, rig.frame.server(), rig.text()[-2000:])
        rig.sec.write_camera(fx.synth_camera(frame=f))
        f += 1
        time.sleep(0.1)
    srv = _until(rig, p, lambda: (lambda s: s if s["state"] == 4 else None)(rig.frame.server()), 30, "state FAILED")
    rig.wait_log(r"LOUD: renderer FAILED \(REBUILDS\)", timeout=90)
    stop.set()
    assert srv["state"] == 4 and srv["failReason"] == 4 and srv["deviceLostTotal"] == 3, srv
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert "result: --stop-event set after" in out, out[-2000:]
    assert "result: process alive at exit" in out
    s, line = fx.summary(out)
    assert s["state"] == "FAILED" and s["lost devices"] == 3, line


@pytest.mark.parametrize("switch,pattern", [("render_test_throw", r"C\+\+ exception: render_test_throw"),
                                            ("render_test_av", r"SEH exception 0xC0000005 at .*\(access violation\)")])
def test_exception_on_render_thread_fails_renderer_not_process(rig, switch, pattern):
    """A C++ exception or an access violation on the render thread is caught there: VPFS_FAILED (VPFSF_EXCEPTION), no
    heartbeat after it, the process goes on (RemoteMain's unhandled-exception filter would end the game with exit 2)."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=5, **{switch: 2})
    rig.wait_log(r"-> OK")
    cameras(rig, 2, 6, hz=20)
    rig.wait_log(r"LOUD: renderer FAILED \(EXCEPTION\)")
    assert re.search(pattern, rig.text()), rig.text()[-2000:]
    srv = rig.frame.server()
    assert srv["state"] == 4 and srv["failReason"] == 5, srv
    hb = srv["heartbeatQpc"]
    time.sleep(0.5)
    assert rig.frame.server()["heartbeatQpc"] == hb      # no heartbeat after FAILED
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert "result: process alive at exit" in out
    s, line = fx.summary(out)
    assert s["state"] == "FAILED" and s["exceptions"] == 1, line


def test_exe_missing_fails_renderer(rig, tmp_path):
    """Without the EXE's shaders the terrain renderer cannot start: FAILED (RENDERER), LOUD, the process goes on."""
    rig.exe = tmp_path / "no_such_CivilizationV_DX11.exe"
    p = rig.start(seconds=2)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert re.search(r"LOUD: renderer FAILED \(RENDERER\)", rig.text()), rig.text()[-2000:]
    assert rig.frame.server()["failReason"] == 3


# ---------------------------------------------------------------------------------------------------------------
# ticket 0600 (D118, D125, D213): every terminal failure of the renderer, and every refused start, takes D125's route once
# (HostConfig::fatal, exit code 5 in server.exe: SrvRemote::ReportAndExit; here the test host's --fatal-hook 1, which prints
# "result: fatal hook code C: WHY", returns, and lets the process go on so that the test reads the count). A device loss the
# renderer recovers from, and a bad options file, call nothing. research/05-graphics-composition/renderer-failure-modes.md.

HOOK = ["--fatal-hook", "1"]
# the scene modules whose own fatal steps would fire in this rig (no game install, no overlay, ice or shadow tap) and
# block drawing (the fog): off, as test_decal_wiring.py does (issue 4310), so that only the renderer's route is seen
OFF = dict(render_fow=0, render_overlay=0, render_ice=0, render_shadow=0)


def fatal_lines(out):
    return re.findall(r"result: fatal hook code (\d+): (.*)", out)


def assert_one_fatal(out, when, reason, *needles):
    """Exactly one fatal call, code 5, naming when (init / in play), the VPFSF_ reason and each needle; the count line."""
    f = fatal_lines(out)
    assert len(f) == 1 and f[0][0] == "5", out[-3000:]
    assert ("FAILED %s (VPFSF_%s)" % (when, reason)) in f[0][1], f
    for n in needles:
        assert n in f[0][1], f
    assert "result: fatal hook calls 1" in out, out[-2000:]
    return f[0][1]


def run_until_fatal(rig, timeout=60, **keys):
    """Starts the host with the hook and a stop event, waits for the hook's line, stops it the orderly way (exit 0) and
    returns its stdout (the hook's line and the count line)."""
    stop, extra = rig.stop_event()
    p = rig.start(seconds=timeout + 30, extra=HOOK + extra, **dict(OFF, **keys))
    rig.wait_stdout(r"result: fatal hook code", timeout=timeout, p=p)
    time.sleep(0.5)   # a second call would come here
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    return rig.stdout()


@pytest.mark.parametrize("value,reason,code", [("adapter", "ADAPTER", 1), ("device", "DEVICE", 2), ("renderer", "RENDERER", 3),
                                               ("section", "SECTION", 6), ("exception", "EXCEPTION", 5)])
def test_init_failure_takes_d125_route(rig, value, reason, code):
    """render_test_init_fail (TEST ONLY) forces each reason at the first device, as the live failures give it (0590's
    C.2a: RENDERER, the shader section of another build): one LOUD 'renderer FAILED (X) ... at init' line, then the fatal
    hook once with code 5, the reason and the counts; draws 0. Without the hook (server.exe always has it) the process
    would go on, which is what left the grey map."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    out = run_until_fatal(rig, render_test_init_fail=value)
    t = rig.text()
    assert re.search(r"LOUD: renderer FAILED \(%s\): .*at init: the 64-bit path cannot draw the world, the server ends" % reason, t), t[-2000:]
    assert "LOUD: D125 route: the server's renderer FAILED at init" in t, t[-2000:]
    why = assert_one_fatal(out, "at init", reason, "draws 0", "lost devices 0")
    assert ("render_test_init_fail=%s (TEST ONLY)" % reason if value != "exception"
            else "C++ exception: render_test_init_fail=exception (TEST ONLY)") in why, why
    s, line = fx.summary(out)
    assert s["state"] == "FAILED" and s["draws"] == 0, line
    if code != 6:   # VPFSF_SECTION never writes into the block (it may belong to another live server)
        srv = rig.frame.server()
        assert srv["state"] == 4 and srv["failReason"] == code, srv


def test_init_failure_without_hook_is_loud_only(rig):
    """No fatal hook (the test host's default): the LOUD line says so and the process goes on; nothing calls a route."""
    p = rig.start(seconds=2, render_test_init_fail="device")
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert re.search(r"LOUD: renderer FAILED \(DEVICE\): .*no fatal hook \(a test host\): the process goes on", rig.text()), rig.text()[-2000:]
    assert "fatal hook code" not in rig.stdout() and "D125 route" not in rig.text()


def test_init_fail_key_refused(rig):
    """A render_test_init_fail value that names no reason refuses the configuration (as every malformed render_* key)."""
    p = rig.start(seconds=1, render_test_init_fail="grey")
    assert rig.finish(p) == 2
    assert "result: config refused: render_test_init_fail=grey: " in rig.stdout(), rig.stdout()


def test_missing_exe_takes_d125_route(rig, tmp_path):
    """The real RENDERER case without a test key: render_exe names no file (no shader section to read)."""
    rig.exe = tmp_path / "no_such_CivilizationV_DX11.exe"
    assert_one_fatal(run_until_fatal(rig), "at init", "RENDERER", "terrain renderer: ")


@pytest.mark.parametrize("kind", ["pe_without_shader_section", "truncated_exe"])
def test_damaged_exe_takes_d125_route(rig, tmp_path, built, exe, kind):
    """A damaged shader source, made from files that are not the game's: a PE image without the EXE's .shader section
    (this test's own render_host_test.exe, copied) and the first 64 KiB of the game EXE copied to a scratch file (headers
    only, no section data; the game file itself is only read). Both: RENDERER at init, the route once."""
    bad = tmp_path / ("damaged_%s.exe" % kind)
    if kind == "pe_without_shader_section":
        bad.write_bytes((built / "render_host_test.exe").read_bytes())
    else:
        with open(exe, "rb") as f:
            bad.write_bytes(f.read(65536))
    rig.exe = bad
    assert_one_fatal(run_until_fatal(rig), "at init", "RENDERER", "terrain renderer: ")


def test_absent_adapter_luid_takes_d125_route(rig):
    """The real ADAPTER case without a test key: render_luid names a LUID no adapter has (with render_luid set there is
    no fallback to the largest adapter)."""
    assert_one_fatal(run_until_fatal(rig, render_luid="7FFFFFFF7FFFFFFF"), "at init", "ADAPTER", "no adapter")


def test_refused_start_takes_d125_route(rig):
    """A HostStart refusal (here: the section is served by another live server) calls the hook once before HostStart
    returns false (test host exit 3); the first server goes on and calls nothing."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    stop, extra = rig.stop_event()
    p1 = rig.start(seconds=60, extra=HOOK + extra, **OFF)
    rig.wait_log(r"state STARTING -> READY")
    pid1 = rig.frame.server()["serverPid"]
    second = rig.tmp / "second.out"
    p2 = rig.start(seconds=1, out=second, extra=HOOK, **OFF)
    assert rig.finish(p2) == 3
    out2 = second.read_text(errors="replace")
    f = fatal_lines(out2)
    assert len(f) == 1 and f[0][0] == "5" and ("the server's renderer could not start: the section is served by another live "
                                               "server, pid %d" % pid1) in f[0][1], out2
    stop.set()
    assert rig.finish(p1) == 0
    assert "result: fatal hook calls 0" in rig.stdout(), rig.stdout()[-2000:]


def test_in_play_rebuild_limit_takes_d125_route(rig):
    """D213: the rebuild limit (3 lost devices in 5 minutes) after the first READY ends by the route once, 'in play',
    with the counts; the two recovered losses before it called nothing."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=240, render_test_device_lost=1, render_rebuild_backoff_ms=20, extra=HOOK + extra, **OFF)
    rig.wait_log(r"-> OK", timeout=90)
    t_end = time.time() + 120
    f = 2
    while "result: fatal hook code" not in rig.stdout():
        assert p.alive() and time.time() < t_end, "no fatal call after %d cameras: %s" % (f - 1, rig.text()[-2000:])
        rig.sec.write_camera(fx.synth_camera(frame=f))
        f += 1
        time.sleep(0.1)
    stop.set()
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert_one_fatal(out, "in play", "REBUILDS", "3 lost devices in 5 minutes", "lost devices 3", "rebuilds 2")
    assert re.search(r"LOUD: renderer FAILED \(REBUILDS\): .*in play: the world has no terrain from here, the server ends", rig.text())


@pytest.mark.parametrize("switch,needle", [("render_test_throw", "C++ exception: render_test_throw"),
                                           ("render_test_av", "SEH exception 0xC0000005")])
def test_in_play_exception_takes_d125_route(rig, switch, needle):
    """D213: an exception on the render thread in play ends by the route once; the text carries the exception's code."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=60, extra=HOOK + extra, **dict(OFF, **{switch: 2}))
    rig.wait_log(r"-> OK")
    t_end = time.time() + 60
    f = 2
    while "result: fatal hook code" not in rig.stdout():
        assert p.alive() and time.time() < t_end, rig.text()[-2000:]
        rig.sec.write_camera(fx.synth_camera(frame=f))
        f += 1
        time.sleep(0.05)
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert_one_fatal(rig.stdout(), "in play", "EXCEPTION", needle, "exceptions 1")


def test_recovered_device_loss_calls_nothing(rig):
    """D213 condition 1: a lost device the renderer recovers from is no failure: READY again, the hook never called."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=240, render_test_device_lost=3, render_rebuild_backoff_ms=20, extra=HOOK + extra, **OFF)
    rig.wait_log(r"-> OK", timeout=90)
    t_end = time.time() + 90
    f = 2
    while "rebuild: device and renderer created again" not in rig.text():
        assert p.alive() and time.time() < t_end, rig.text()[-2000:]
        rig.sec.write_camera(fx.synth_camera(frame=f))
        f += 1
        time.sleep(0.1)
    _until(rig, p, lambda: rig.frame.server()["state"] == 2 or None, 30, "state READY again")
    stop.set()
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert "result: fatal hook calls 0" in out and "fatal hook code" not in out, out[-2000:]


def test_bad_options_file_is_no_renderer_failure(rig, tmp_path):
    """'A bad options file' (VP64Options.ini) is not a renderer failure: the loader warns and takes the default
    (options/src/vp64_options.cpp), the renderer reaches READY and nothing calls the route."""
    opt = tmp_path / "VP64Options.ini"
    opt.write_text("[Renderer]\nBakedTileCameraRadius = banana\nthis line is not ini\n\x00\xff garbage\n", encoding="latin-1")
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    p = rig.start(seconds=60, options_file=opt, extra=HOOK + extra, **OFF)
    rig.wait_log(r"state STARTING -> READY", timeout=60)
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    out = rig.stdout()
    assert re.search(r"options: .*: status \d+, [1-9]\d* warnings, BakedTileCameraRadius -1", out), out[-2000:]
    assert "result: fatal hook calls 0" in out and "fatal hook code" not in out, out[-2000:]


# ---------------------------------------------------------------------------------------------------------------
# refusals

@pytest.mark.parametrize("key,value", [("render_handover", "keyedmutex"), ("render_ring_slots", "5"), ("render_ring_slots", "1"),
                                       ("render_size", "10x10"), ("render_size", "1920"), ("render_sync", "gpu"),
                                       ("render_depth", "2"), ("render_tess", "abc"), ("render_tess", "65"),
                                       ("render_luid", "zz"), ("render_rebuild_backoff_ms", "5"), ("render_pass", "3"),
                                       ("render_color", "purple"), ("render_fallback", "caches"),
                                       ("render_constant_rgba", "0x1FFFFFFFF"), ("render_tile_plots", "4"),
                                       ("render_bake_no_sleep", "2"), ("render_bake_no_sleep", "on"),   # ticket 0414
                                       ("render_prevent_driver_threading", "2"),   # ticket 0435
                                       ("render_prevent_driver_threading", "on"),
                                       ("render_test_units_charge_any", "2"),   # ticket 4737 (rule A)
                                       ("render_test_units_charge_any", "on"),
                                       ("render_test_units_tier_gate", "2"),   # ticket 4737
                                       ("render_test_units_turn_first", "2"),   # ticket 4735 (rule A)
                                       ("render_test_units_turn_first", "on"),
                                       ("render_test_units_quick_combat", "2"),   # ticket 0796 (rule A; TEST ONLY)
                                       ("render_test_units_quick_combat", "on"),
                                       ("render_units_fight_drawn_figures", "2"),   # ticket 4744 (rule A)
                                       ("render_units_fight_drawn_figures", "on"),
                                       ("render_test_units_reform_first", "2"),   # tickets 4745 + 4747 (rule A)
                                       ("render_test_units_reform_first", "on"),
                                       ("render_test_units_walk_home_in_fight", "2"),   # ticket 4742 (rule A)
                                       ("render_test_units_walk_home_in_fight", "on"),
                                       ("render_test_units_city_in_place", "2"),   # ticket 4748 (rule A)
                                       ("render_test_units_city_in_place", "on"),
                                       ("render_test_units_ready_after_move", "2"),   # ticket 4746 (rule A)
                                       ("render_test_units_ready_after_move", "on"),
                                       ("render_test_units_defend_from_stance", "2"),   # ticket 4750 (rule A)
                                       ("render_test_units_defend_from_stance", "on"),
                                       ("render_test_units_reform_ends_at_arrival", "2"),   # ticket 5050 (rule A)
                                       ("render_test_units_reform_ends_at_arrival", "on"),
                                       ("render_test_units_attack_from_stance", "2"),   # ticket 5050 (rule A)
                                       ("render_test_units_attack_from_stance", "on"),
                                       ("render_units_rush_ride_through", "2"),   # ticket 5057 (rule A)
                                       ("render_units_rush_ride_through", "on"),
                                       ("render_units_defender_waits_first_landing", "2"),   # ticket 5057 (rule A)
                                       ("render_units_defender_waits_first_landing", "on"),
                                       ("render_test_units_death_by_variant", "2"),   # ticket 5053 (rule A)
                                       ("render_test_units_death_by_variant", "on"),
                                       ("render_test_units_end_after_dying", "2"),   # ticket 5053 (rule A)
                                       ("render_test_units_end_after_dying", "on"),
                                       ("render_units_victory_word", "2"),   # ticket 5059 (rule A)
                                       ("render_units_victory_word", "on"),
                                       ("render_units_type2_phase2", "2"),   # ticket 5067 S1 (rule A)
                                       ("render_units_type2_phase2", "on"),
                                       ("render_units_melee_reach", "2"),   # ticket 0743 (rule A)
                                       ("render_units_melee_reach", "on"),
                                       ("render_units_defender_step", "2"),   # ticket 5061 (rule A)
                                       ("render_units_defender_step", "on"),
                                       ("render_units_orders_after_walk", "2"),   # ticket 5065 (rule A)
                                       ("render_units_orders_after_walk", "on"),
                                       ("render_test_units_trigger_striker_weapon", "2"),   # ticket 5052 (rule A)
                                       ("render_test_units_trigger_striker_weapon", "on"),
                                       ("render_test_units_trigger_terrain", "2"),   # ticket 5052 (rule A; default 1 since W89, MC #11356: only 0 and 1 start)
                                       ("render_test_units_trigger_terrain", "on"),
                                       ("render_units_projectile_pose", "2"),   # ticket 5064 (rule A; default 1: only 0 and 1 start)
                                       ("render_units_projectile_pose", "on"),
                                       ("render_units_trigger_striker_melee", "2"),   # ticket 5055 (rule A; default 1, MC #11607: only 0 and 1 start)
                                       ("render_units_trigger_striker_melee", "on"),
                                       ("render_units_trigger_city_era", "2"),   # ticket 5058 (rule A)
                                       ("render_units_trigger_city_era", "on"),
                                       ("render_units_fight_after_walk", "2"),   # ticket 5066 (rule A)
                                       ("render_units_fight_after_walk", "on"),
                                       ("render_units_trigger_city_shot", "2"),   # ticket 5062 (rule A)
                                       ("render_units_trigger_city_shot", "on"),
                                       ("render_prepare", "2"),    # ticket 0713
                                       ("render_prepare", "on")])
def test_config_refused(rig, key, value):
    """A malformed or out-of-range render_* value refuses the start with its key and value (server.exe: a LOUD report
    line, the game goes on without the renderer); nothing is written into the section."""
    p = rig.start(seconds=1, **{key: value})
    assert rig.finish(p) == 2
    assert re.search(r"result: config refused: %s=%s: " % (re.escape(key), re.escape(value)), rig.stdout()), rig.stdout()
    assert rig.frame.server()["magic"] == 0


def test_second_server_on_one_section_refused(rig):
    """Two renderers never share a section: a second one finds the first one's live pid and is refused before it writes
    anything; the first one goes on."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    p1 = rig.start(seconds=5)
    rig.wait_log(r"state STARTING -> READY")
    pid1 = rig.frame.server()["serverPid"]
    p2 = rig.start(seconds=1, out=rig.tmp / "second.out")
    assert rig.finish(p2) == 3
    second = (rig.tmp / "second.out").read_text(errors="replace")
    assert "served by another live server, pid %d" % pid1 in second, second
    assert rig.frame.server()["serverPid"] == pid1 and rig.frame.server()["state"] == 2
    assert rig.finish(p1) == 0


def test_tap_hash_mismatch_refused(rig):
    """A tap whose FNV-1a 64 does not match is acknowledged with ackResult -1 and not drawn (shell_server's rule).

    Issue 0333: the run lasts until the refusal is logged and two more per-second lines follow it (the loop went on with
    the camera there and drew nothing), not a fixed 2 s: under MSVC ASan beside two parallel ASan suites a 2 s run ended
    once before the tap was read (ack (0, 0))."""
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1, fnv=12345)
    p = rig.start(seconds=60, extra=extra)
    rig.wait_log(r"tap load seq 1 refused \(ackResult -1\)", timeout=50)
    n0 = len(fx.second_lines(rig.text()))
    rig.wait_second_line(lambda d: len(fx.second_lines(rig.text())) >= n0 + 2, timeout=50, p=p)
    stop.set()
    assert rig.finish(p) == 0
    assert fx.tap_ack(rig.sec)[:2] == (1, -1)
    assert re.search(r"tap load seq 1 refused \(ackResult -1\)", rig.text())
    assert fx.totals(rig.text())["drawn"] == 0


# ---------------------------------------------------------------------------------------------------------------
# the bake tap (the first TileCacheSource): render_host_test.exe --bakefake plays the composite's bake tap

def bakefake(rig, tiles="3x3", tile_plots=2, missing=-1, version=1, typeless=0, seconds=20):
    import shellrun
    out = rig.tmp / ("bakefake_%s.out" % rig.tag)
    p = shellrun.launch([str(rig.built / "render_host_test.exe"), "--bakefake", rig.section, tiles, str(tile_plots), str(missing),
                         str(version), str(typeless), str(seconds)], rig.desktop, console=True, out_path=out)
    rig.procs.append(p)
    t_end = time.time() + fx.WAIT_S   # ticket 0607: a bound on the event (was 15 s), as the rig's waits since 0496
    while time.time() < t_end:
        if out.exists() and "ACTIVE" in out.read_text(errors="replace"):
            return p
        assert p.alive(), out.read_text(errors="replace") if out.exists() else "bakefake died"
        time.sleep(0.05)
    raise TimeoutError("bakefake did not start")


@pytest.mark.parametrize("typeless", [0, 1])
def test_cache_textures_used(rig, typeless):
    """The bake-tap block (ACTIVE) and its manifest: each slot's shared textures are opened by name (a typeless texture
    with a typed view, issue 3206) and set on its tile; the EXE's pass-1 PS draws the cache colour (red on even tiles,
    blue on odd ones); the manifest is acknowledged."""
    bakefake(rig, typeless=typeless)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=4, render_test_dump=str(rig.dump), render_test_dump_after=1)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert re.search(r"bake manifest seq 1 applied: 9 slots, 9 opened, 0 missing", t), t[-2000:]
    assert struct.unpack("<I", rig.sec.read(fx.BAKETAP_OFFSET + 200, 4))[0] == 1
    a = fx.image(rig.dump).astype(int)
    red = (a[..., 0] > 200) & (a[..., 1] < 40) & (a[..., 2] < 40)
    blue = (a[..., 2] > 200) & (a[..., 0] < 40) & (a[..., 1] < 40)
    assert red.sum() > 2000 and blue.sum() > 2000, (red.sum(), blue.sum())


def test_missing_cache_texture_uses_fallback(rig):
    """A slot whose textures do not open is counted missing; its tile is drawn with the fallback; the rest are cached."""
    bakefake(rig, missing=4)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=4)
    assert rig.finish(p) == 0
    t = rig.text()
    assert re.search(r"bake manifest seq 1 applied: 9 slots, 8 opened, 1 missing", t), t[-2000:]
    assert fx.totals(t)["cache missing"] == 1 and fx.totals(t)["drawn"] >= 1


def test_wrong_manifest_version_refused(rig):
    """A manifest of another version is refused (once, logged), not applied, and the terrain is still drawn."""
    bakefake(rig, version=2)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=3)
    assert rig.finish(p) == 0
    t = rig.text()
    assert "bake manifest (seq 1) refused: manifest version 2 (want 1)" in t, t[-2000:]
    tot = fx.totals(t)
    assert tot["manifest errors"] >= 1 and tot["cache opened"] == 0 and tot["drawn"] >= 1, tot


def test_tile_source_asks_for_another_tile_size(rig):
    """Issue 3306 (srv_tile_source.h wantedTilePlots, replacing the tap's constructor callback): the field is built at
    the default 2-plot tiles; a bake tap of 1-plot tiles appears later; the source asks for 1, the scene rebuilds the
    tiles from its kept field and the source applies the manifest in the same loop iteration."""
    # ticket 0607: the run ends through its stop event once the manifest is applied (start_until, ticket 0496's pattern);
    # it was --seconds 6, and on a loaded machine the bakefake's start-up and the rebuild outlasted it (TimeoutError on
    # "bake manifest seq 1 applied", 0600's ASan run and 0454's milestone 2a run of 2026-11-20). The bakefake lives
    # until the rig closes it.
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until()
    rig.wait_log(r"tiles of 2 plots", p=p)
    bakefake(rig, tiles="6x6", tile_plots=1, seconds=fx.BOUND_S)
    rig.wait_log(r"bake manifest seq 1 applied: 36 slots", p=p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "asking the scene to rebuild the field's tiles" in t, t[-2000:]
    assert re.search(r"tiles of 1 plots", t), t[-2000:]
    tot = fx.totals(t)
    assert tot["field errors"] == 0 and tot["manifest errors"] == 0, tot


def test_tile_size_fixed_by_config_refuses_the_tap(rig):
    """Negative: render_tile_plots=2 fixes the tile size; a bake tap of 1-plot tiles is refused (logged once), its
    caches are not opened, the field keeps its 2-plot tiles and the terrain is still drawn."""
    bakefake(rig, tiles="6x6", tile_plots=1)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=3, render_tile_plots=2)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "tile plots 1 against render_tile_plots 2" in t, t[-2000:]
    assert not re.search(r"tiles of 1 plots", t), t[-2000:]
    tot = fx.totals(t)
    assert tot["cache opened"] == 0 and tot["manifest errors"] >= 1 and tot["drawn"] >= 1, tot


# ---------------------------------------------------------------------------------------------------------------
# the ring hand-over (render_handover=ring): the host drives shell/frame's RingSink

def test_ring_handover_slots_ready_per_published_camera(rig):
    """render_handover=ring: the composite's request (4 slots, CPU sync, the target size) gets a generation; each
    published camera becomes a READY slot record with its frame id, drawResult OK and tiles drawn; latestFrameId follows
    the published frames.

    Issue 0413: counted, not timed. The test published a camera every 30 ms in a 6 s process and sampled the four slots
    between publishes, asserting 20 distinct frame ids seen; on a loaded workstation the sampling saw 15 (0405's
    whole-file run). Now each camera is published after the previous one is the ring's latest frame, and then the slot
    record of exactly that frame is read: every published camera 2..39 must be found READY with its own frame id (the
    old check took any 20 of 38 and any frame id). The run ends through --stop-event (research/05-graphics-composition/
    renderer-in-server-design.md 4.19.1)."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    stop, extra = rig.stop_event()
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start(seconds=240, render_handover="ring", render_sync="cpu", extra=extra)
    rig.wait_log(r"tap load seq 1 read", timeout=90)
    rig.frame.request(4, fx.W, fx.H, 2)
    rig.wait_log(r"ring sink: generation \d+: 4 slots 320x240 sync cpu", timeout=90)
    for f in range(2, 40):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 5)))
        rig.frame.publish(f)
        cam.set()
        # the sink writes the slot record, then latestSlot, then latestFrameId: once latestFrameId is f the record is whole
        _until(rig, p, lambda: rig.frame.ring()["latestFrameId"] >= f, 90, "camera %d the ring's latest frame" % f)
        recs = [rig.frame.slot(i) for i in range(4)]
        # the newest slot is never the next draw's target; a redraw of the same camera may add a second READY record of f
        mine = [s for s in recs if s["frameId"] == f and s["state"] == 2 and s["seq"] % 2 == 0]
        assert mine, ("no READY slot record of frame %d" % f, recs)
        for s in mine:
            assert s["drawResult"] == 0 and s["tilesDrawn"] > 0, s
            # issue 0139: the synthetic field is land everywhere (z >= 2.4) and this test taps no cache, so every
            # drawn tile is a land tile without its bake, counted by the renderer at the draw
            assert s["unbakedCounted"] == 1 and s["tilesUnbaked"] == s["tilesDrawn"], s
    ring = rig.frame.ring()
    assert ring["slotCount"] == 4 and ring["sync"] == 2 and ring["latestFrameId"] == 39, ring
    stop.set()
    assert rig.finish(p, timeout=120) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    s, line = fx.summary(rig.stdout())
    assert s["published"] >= 30 and s["exceptions"] == 0, line
    assert "handover ring" in line


# ---------------------------------------------------------------------------------------------------------------
# server/run_server.py --remote-config writes the render keys

def _run_server(tmp_path, *extra):
    cmd = [sys.executable, "-I", str(REPO / "server" / "run_server.py"), "--turns", "1", "--run", str(tmp_path / "run"),
           "--bin", str(tmp_path / "bin"), "--remote-config", str(tmp_path / "server.cfg"), "--loc-db", "none"] + list(extra)
    return subprocess.run(cmd, capture_output=True, text=True, creationflags=0x08000000)


def test_run_server_writes_render_keys(tmp_path):
    cp = _run_server(tmp_path, "--render", "1", "--render-opt", "render_handover=ring", "--render-opt", "render_section=Local\\X")
    assert cp.returncode == 0, cp.stdout + cp.stderr
    lines = (tmp_path / "server.cfg").read_text().splitlines()
    assert "render=1" in lines and "render_handover=ring" in lines and "render_section=Local\\X" in lines, lines


@pytest.mark.parametrize("opt", ["handover=ring", "render_=1", "render_handover"])
def test_run_server_render_opt_refused(tmp_path, opt):
    cp = _run_server(tmp_path, "--render-opt", opt)
    assert cp.returncode == 2 and "want render_KEY=VALUE" in cp.stderr, cp.stdout + cp.stderr
    assert not (tmp_path / "server.cfg").exists()


# ---------------------------------------------------------------------------------------------------------------
# the render thread cannot name a DLL type (build check)

def _build_server():
    spec = importlib.util.spec_from_file_location("build_server_mod", REPO / "server" / "build_server.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def test_render_sources_include_no_dll_header():
    """No file of server/src/render includes a DLL-facing header of the server or a fork header."""
    bad = re.compile(r'#include\s+[<"](srv_common\.h|srv_remote\.h|srv_rt\.h|vp_ids\.gen\.h|server_hooks\.gen\.h|Cv[^">]*|'
                     r'ICv[^">]*|FireWorks/[^">]*|lua\.h)[">]')
    hits = []
    for f in sorted(p for p in (REPO / "server" / "src" / "render").rglob("*") if p.is_file()):
        for i, ln in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if bad.search(ln):
                hits.append("%s:%d: %s" % (f.name, i, ln.strip()))
    assert not hits, hits


def test_render_compile_args_have_no_fork_path_and_refuse_a_dll_header(tmp_path):
    """The render units' compile flags (build_server.render_compile_args) carry no fork include path: a probe that
    includes srv_common.h (the server's DLL-facing header) fails to compile with them, the render host header compiles."""
    bs = _build_server()
    args = bs.render_compile_args("release", tmp_path)
    assert not any("Community-Patch" in a or "src-76992d23" in a for a in args), args
    x64 = bs.x64build
    env = x64.load_vcvars_env(lambda m, echo=True: None, Path("D:/c5-64bit-work/3300/w1/vcvars-env.json"))
    good, badp = tmp_path / "probe_ok.cpp", tmp_path / "probe_dll.cpp"
    good.write_text('#include "srv_render_host.h"\n#include "feed/srv_feed.h"\nint probe() { return (int)srvrender::HostState(); }\n')
    badp.write_text('#include "srv_common.h"\nint probe() { return 0; }\n')
    base = [str(x64.CLANG_CL)] + [a for a in args if a != "/c"] + ["-fsyntax-only"]
    ok = subprocess.run(base + [str(good)], capture_output=True, text=True, env=env, cwd=str(tmp_path), creationflags=0x08000000)
    assert ok.returncode == 0, ok.stdout + ok.stderr
    no = subprocess.run(base + [str(badp)], capture_output=True, text=True, env=env, cwd=str(tmp_path), creationflags=0x08000000)
    assert no.returncode != 0 and "file not found" in (no.stdout + no.stderr), no.stdout + no.stderr


def test_render_units_include_the_server_options_header_by_name(tmp_path):
    """Issue 3312: options/include is in build_server.RENDER_INCLUDES, so srv_render_options.cpp includes srv_options.h by
    name (no relative path, no declaration of its own). The real unit compiles with the render flags; without the options
    folder (the flags before 3312) it fails with 'file not found'."""
    src = (REPO / "server" / "src" / "render" / "srv_render_options.cpp").read_text(encoding="utf-8")
    assert '#include "srv_options.h"' in src and "../" not in src, src
    assert "const Vp64Opt::Options& SrvOptions();" not in src, src
    bs = _build_server()
    assert REPO / "options" / "include" in bs.RENDER_INCLUDES, bs.RENDER_INCLUDES
    args = bs.render_compile_args("release", tmp_path)
    x64 = bs.x64build
    env = x64.load_vcvars_env(lambda m, echo=True: None, Path("D:/c5-64bit-work/3300/w1/vcvars-env.json"))
    unit = REPO / "server" / "src" / "render" / "srv_render_options.cpp"
    base = [str(x64.CLANG_CL)] + [a for a in args if a != "/c"] + ["-fsyntax-only"]
    ok = subprocess.run(base + [str(unit)], capture_output=True, text=True, env=env, cwd=str(tmp_path), creationflags=0x08000000)
    assert ok.returncode == 0, ok.stdout + ok.stderr
    opt_inc = "/I%s" % (REPO / "options" / "include")
    assert opt_inc in args, args
    old = [a for a in base if a != opt_inc]
    no = subprocess.run(old + [str(unit)], capture_output=True, text=True, env=env, cwd=str(tmp_path), creationflags=0x08000000)
    assert no.returncode != 0 and "file not found" in (no.stdout + no.stderr), no.stdout + no.stderr
