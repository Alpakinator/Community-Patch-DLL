"""Ticket 0703: idle bakes fitted between cameras (ResidencySourceConfig::bakeFit, C5_RENDER_BAKE_FIT; research/05-graphics-
composition/terrain-bake-residency.md 8). While the camera moves, an idle() slice starts a bake only when it is expected
to end before the next camera, so a camera seldom finds the render thread inside an idle bake; a bake that does not fit
waits, and one is admitted at the latest 250 ms after the last admitted slice. Off by default (mode 0, after the A/B of
2026-11-23); the cases of the fit set C5_RENDER_BAKE_FIT=1.

The rig of test_bake_yield.py (render_host_test.exe = the render objects server.exe links; fake composite on a hidden
desktop; the stand-in baker slowed by render_test_bake_cpu_ms). The cameras look away from the map, so every bake is an
idle() one. Every negative case checks the outputs and the exit code.
"""
import os
import re
import threading
import time

import pytest

import renderfx as fx

BAKE_MS = 8.0       # one stand-in bake
BUDGET_MS = 100.0   # one idle slice may hold the thread this long without the yield or the fit
AWAY = 50000.0      # camera x far off the map (6 x 6 plots = 384 units)


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


@pytest.fixture
def fit_env():
    """Sets C5_RENDER_BAKE_FIT for the host started in the test (inherited), restored after."""
    old = os.environ.get("C5_RENDER_BAKE_FIT")

    def put(v):
        if v is None:
            os.environ.pop("C5_RENDER_BAKE_FIT", None)
        else:
            os.environ["C5_RENDER_BAKE_FIT"] = v
    yield put
    put(old)


def fit_lines(text):
    """The 'residency fit 1s:' lines as dicts: the per-second part, and the totals after '| tot' as 'tot_<name>'."""
    out = []
    for ln in text.splitlines():
        if "residency fit 1s:" not in ln:
            continue
        head, _, tot = ln.partition("| tot")
        d = {k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", head)}
        d.update({"tot_" + k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", tot)})
        out.append(d)
    return out


class Cameras:
    """Writes a camera every 1/hz s on a thread; moving: the pan changes with each frame (the residency's moving test is a
    changed view matrix), else the same view every frame."""

    def __init__(self, rig, hz, moving):
        self.rig, self.hz, self.moving = rig, hz, moving
        self.stop = threading.Event()
        self.th = threading.Thread(target=self.run, daemon=True)

    def run(self):
        f = 2
        while not self.stop.is_set():
            self.rig.sec.write_camera(fx.synth_camera(frame=f, pan=AWAY + (f % 7 if self.moving else 0)))
            f += 1
            time.sleep(1.0 / self.hz)

    def __enter__(self):
        self.th.start()
        return self

    def __exit__(self, *a):
        self.stop.set()
        self.th.join(5)


def start(rig, **keys):
    rig.sec.write_camera(fx.synth_camera(frame=1, pan=AWAY))
    rig.tap(load_seq=1)
    return rig.start_until(render_tiles="bake", render_baker="standin", render_test_bake_cpu_ms=BAKE_MS,
                           render_bake_budget_ms=BUDGET_MS, render_bake_rate=100000, **keys)


def run_to_ahead_done(rig, hz, moving, fit_line=True, **keys):
    """A slow bake-ahead (36 tiles x BAKE_MS) while cameras arrive at hz; returns (the last residency line, the fit lines).
    fit_line: also wait for a 'residency fit 1s:' line (the fit writes its line a second after its first call with work,
    and the bake-ahead may end sooner)."""
    p = start(rig, **keys)
    with Cameras(rig, hz, moving):
        rig.wait_residency(fx.ahead_done, p=p)
        if fit_line:
            rig.wait_log(r"residency fit 1s:", p=p)
        assert rig.stop(p) == 0, rig.stdout()[-2000:]
    lines = fx.residency_lines(rig.text())
    assert lines, rig.text()[-2000:]
    last = lines[-1]
    assert last["land"] == 36 and last["ahead_done"] == 1 and last["failed"] == 0, last
    return last, fit_lines(rig.text())


def test_fit_cuts_the_slice_to_the_camera_gap(rig, fit_env):
    """Mode 1, a moving camera every 25 ms, 8-ms bakes: the slices are admitted with their budget cut to the window
    before the next camera, so most end before a camera comes (few yields), and the bake-ahead completes."""
    fit_env("1")
    last, fl = run_to_ahead_done(rig, hz=40.0, moving=True)
    text = rig.text()
    assert "residency: idle bakes fitted between cameras: mode 1 (while the camera moves" in text, text[:3000]
    assert fl, text[-3000:]
    tot = fl[-1]
    assert tot["mode"] == 1, tot
    fitted = tot["tot_cut"] + tot["tot_admitted"]
    assert fitted >= 3 and tot["tot_fit_bakes"] >= 3, tot
    # the estimates: a bake of the stand-in takes BAKE_MS or a little more (ASan, load); the gap is under the 25 ms period
    est = [d for d in fl if d["bake_ms"] > 0 and d["gap_ms"] >= 0]
    assert est, fl
    assert BAKE_MS * 0.9 <= est[-1]["bake_ms"] < BUDGET_MS / 2, est[-1]
    assert 0 <= est[-1]["gap_ms"] < 1000.0 / 40.0 * 2, est[-1]
    # a slice that ends inside the budget before a camera comes is no yield: far fewer yields than slices
    print("fit:", tot, "residency:", {k: last.get(k) for k in ("idle_slices", "yields", "yield_wait_max_ms", "idle_slice_max_ms")})
    assert last["idle_slices"] >= 4, last
    assert last["yields"] < 0.5 * last["idle_slices"], (last, tot)
    assert last["idle_slice_max_ms"] < BUDGET_MS / 2, last


def test_fit_off_keeps_the_slice_until_a_camera(rig, fit_env):
    """Negative and the default: without C5_RENDER_BAKE_FIT (mode 0) the slice before 0703, which runs until a camera
    waits (most slices end by a yield); no fit line is written."""
    fit_env(None)
    last, fl = run_to_ahead_done(rig, hz=40.0, moving=True, fit_line=False)
    text = rig.text()
    assert "residency: idle bakes fitted between cameras: mode 0 (off: the slices before 0703" in text, text[:3000]
    assert fl == [], fl[:3]
    print("residency:", {k: last.get(k) for k in ("idle_slices", "yields", "yield_wait_max_ms", "idle_slice_max_ms")})
    assert last["idle_slices"] >= 4 and last["yields"] >= 0.5 * last["idle_slices"], last


def test_fit_needs_the_host_yield(rig, fit_env):
    """Negative: render_bake_yield=0: the fit is off whatever the switch says (the line names why), no fit line."""
    fit_env("2")
    p = start(rig, render_bake_yield=0)
    with Cameras(rig, 40.0, True):
        rig.wait_log(r"residency: idle bakes fitted between cameras: off \(render_bake_yield=0 or no host yield; mode 2 asked",
                     p=p)
        rig.wait_residency(fx.ahead_done, p=p)
        assert rig.stop(p) == 0, rig.stdout()[-2000:]
    assert fit_lines(rig.text()) == []


def test_fit_bad_switch_value_keeps_the_default(rig, fit_env):
    """Negative: C5_RENDER_BAKE_FIT=7 is named LOUD and the default (mode 0) is used."""
    fit_env("7")
    p = start(rig)
    with Cameras(rig, 40.0, True):
        rig.wait_log(r"LOUD: residency: C5_RENDER_BAKE_FIT is not 0, 1 or 2: the default \(0\) is used", p=p)
        rig.wait_log(r"residency: idle bakes fitted between cameras: mode 0 ", p=p)
        assert rig.stop(p) == 0, rig.stdout()[-2000:]


def test_fit_at_rest_keeps_the_slice(rig, fit_env):
    """Mode 1 with a camera that does not move: every slice with a budget is the slice before 0703 (legacy), none is
    fitted, deferred or forced."""
    fit_env("1")
    last, fl = run_to_ahead_done(rig, hz=40.0, moving=False)
    assert fl, rig.text()[-3000:]
    tot = fl[-1]
    assert tot["tot_legacy"] >= 1, tot
    assert tot["tot_admitted"] == 0 and tot["tot_cut"] == 0 and tot["tot_deferred"] == 0 and tot["tot_forced"] == 0, tot
    assert all(d["moving"] == 0 for d in fl), fl


def test_fit_bound_admits_one_bake_when_no_gap_fits(rig, fit_env):
    """Cameras faster than a bake (every ~2 ms against 8-ms bakes): no bake fits a gap, so the bakes wait (deferred) and
    the bound admits one bake at a time (forced), about four a second; the bake-ahead goes on."""
    fit_env("1")
    p = start(rig)
    with Cameras(rig, 500.0, True):
        rig.wait_log(r"residency fit 1s: .*\| tot .*forced=([4-9]|\d\d+) ", p=p, timeout=fx.WAIT_S)
        lines = fit_lines(rig.text())
        assert rig.stop(p) == 0, rig.stdout()[-2000:]
    tot = lines[-1]
    print("fit:", tot)
    assert tot["tot_deferred"] > tot["tot_forced"] >= 4, tot
    # a forced call makes one bake (its budget is spent after it); a fitted one may make more
    assert tot["tot_fit_bakes"] >= tot["tot_forced"], tot
    per_s = [d["forced"] for d in lines[1:]]   # the first line may cover less than a second
    assert per_s and max(per_s) <= 1000.0 / 250.0 + 1, lines
