"""Issue 0140: the residency's idle slice on server.exe's render thread yields to a waiting camera (render_bake_yield,
default 1), stale visible tiles are re-baked in the idle slice rather than before the draw (render_bake_stale_before_draw,
default 0), and the counters that bound the wait (yields, yield_wait_max_ms / _mean_ms / _p99_ms, idle_slice_max_ms) in
the residency's part of the `terrain 1s:` line.

The rig of test_residency_source.py (render_host_test.exe = the render objects server.exe links; fake composite on a
hidden desktop). The stand-in baker is slowed by render_test_bake_cpu_ms (StandinFaults::cpuMs, a busy wait per bake) so
that a bake slice is long against the camera period. Every negative case checks the outputs and the exit code.
"""
import re
import threading
import time

import pytest

import renderfx as fx

BAKE_MS = 8.0       # one stand-in bake
BUDGET_MS = 100.0   # one idle slice may hold the thread this long without the yield
AWAY = 50000.0      # camera x far off the map (6 x 6 plots = 384 units)


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


def res_lines(text):
    """The residency part of the per-second terrain lines, as dicts (as test_residency_source.py)."""
    out = []
    for ln in text.splitlines():
        m = re.search(r"\| residency (land=.*?) \|", ln)
        if m:
            out.append({k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", m.group(1))})
    return out


def run_with_cameras(rig, hz=100.0, **keys):
    """A slow bake-ahead (36 tiles x BAKE_MS) while cameras arrive at hz; returns the last residency line. The cameras look
    away from the map (pan AWAY): no tile is visible, so beforeDraw bakes nothing and every bake is an idle() one.
    Ticket 0496: the run ends once a residency line shows the bake-ahead complete (it was a fixed 4 s, which ended before
    0365's 2 s decal hold and the bake-ahead did under ASan on a loaded machine)."""
    rig.sec.write_camera(fx.synth_camera(frame=1, pan=AWAY))
    rig.tap(load_seq=1)
    p = rig.start_until(render_tiles="bake", render_baker="standin", render_test_bake_cpu_ms=BAKE_MS,
                        render_bake_budget_ms=BUDGET_MS, render_bake_rate=100000, **keys)
    stop = threading.Event()
    gap = [0.0]   # ticket 0496: the longest time between two camera writes, ms (the writer can be starved by the load)

    def cams():
        f = 2
        t_last = time.perf_counter()
        while not stop.is_set():
            rig.sec.write_camera(fx.synth_camera(frame=f, pan=AWAY + f % 7))
            now = time.perf_counter()
            gap[0] = max(gap[0], (now - t_last) * 1000.0)
            t_last = now
            f += 1
            time.sleep(1.0 / hz)

    th = threading.Thread(target=cams, daemon=True)
    th.start()
    try:
        rig.wait_residency(fx.ahead_done, p=p)
        assert rig.stop(p) == 0, rig.stdout()[-2000:]
    finally:
        stop.set()
        th.join(5)
    lines = res_lines(rig.text())
    assert lines, rig.text()[-2000:]
    last = lines[-1]
    assert last["land"] == 36 and last["ahead_done"] == 1 and last["failed"] == 0, last
    last["camera_gap_max_ms"] = gap[0]
    print("residency 1s (last):", {k: last.get(k) for k in ("bakes", "idle_slices", "idle_slice_max_ms", "yields", "yield_wait_max_ms", "yield_wait_mean_ms", "yield_wait_p99_ms", "ahead_ms", "bake_cpu_max_ms", "yield", "stale_before_draw", "camera_gap_max_ms")})
    return last


def test_idle_slice_yields_to_a_waiting_camera(rig):
    """render_bake_yield=1 (the default): a camera that arrives while the slice bakes ends it before the next bake, so it
    waits for at most the bake in progress (BAKE_MS), not the slice's budget (BUDGET_MS)."""
    last = run_with_cameras(rig)
    assert last["yield"] == 1 and last["stale_before_draw"] == 0, last
    assert last["yields"] >= 3, last
    # the bound spans one Residency::bakeOne (the baker's bake plus the slice copies), far below the 100-ms budget: under
    # ASan on a loaded machine (2026-11-15) one 8-ms stand-in bake held the bound to 16-24 ms (bake_cpu_max_ms 9.3 counts
    # only the baker's call), so the check is against the budget, not BAKE_MS
    assert 0 < last["yield_wait_max_ms"] < BUDGET_MS / 2, last
    # a slice ends at the first camera that waits; ticket 0496: when the load starved the camera writer for longer than its
    # period (plain run beside 6 load jobs, 2026-11-17: one slice of 107 ms), a slice may run that much longer, up to its
    # budget, because no camera waited
    assert last["idle_slice_max_ms"] < BUDGET_MS / 2 + max(0.0, last["camera_gap_max_ms"] - 1000.0 / 100.0), last
    assert last["yield_wait_p99_ms"] <= last["yield_wait_max_ms"] + 0.25, last
    assert last["gray_after_ahead"] == 0, last


def test_without_the_yield_a_slice_holds_the_thread_for_its_budget(rig):
    """Negative: render_bake_yield=0 keeps the old slice: no yield is counted, and a slice runs to its budget while the
    cameras keep coming (the wait the yield removes); render_bake_stale_before_draw=1 is reported in the line."""
    last = run_with_cameras(rig, render_bake_yield=0, render_bake_stale_before_draw=1)
    assert last["yield"] == 0 and last["stale_before_draw"] == 1, last
    assert last["yields"] == 0 and last["yield_wait_max_ms"] == 0, last
    assert last["idle_slice_max_ms"] >= BUDGET_MS - 1.0, last


@pytest.mark.parametrize("key,value", [("render_bake_yield", "2"), ("render_bake_yield", "on"),
                                       ("render_bake_stale_before_draw", "yes"), ("render_bake_stale_before_draw", "-1"),
                                       ("render_test_bake_cpu_ms", "-1"), ("render_test_bake_cpu_ms", "x"),
                                       ("render_test_bake_cpu_ms", "1001")])
def test_yield_config_refused(rig, key, value):
    """Negative: a malformed key of 0140 refuses the start with its key and value (exit 2), nothing is written into the
    section."""
    p = rig.start(seconds=1, **{key: value})
    assert rig.finish(p) == 2
    assert re.search(r"result: config refused: %s=%s: " % (re.escape(key), re.escape(value)), rig.stdout()), rig.stdout()
    assert rig.frame.server()["magic"] == 0
