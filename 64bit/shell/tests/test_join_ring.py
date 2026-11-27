"""Issue 3306 (epic 3300, item 1): the ring hand-over end to end on the stand-in - server.exe's render host (3301, the same
render objects server.exe links, in server/tests/render's render_host_test.exe) with render_handover=ring against the
composite's ring source (3304, handover=ring) inside test_host.exe, lockstep and depth on, FENCE and CPU. Wave 1 tested
each half only against 3302's stand-ins (ring_producer.exe / ring_consumer.exe).

Builds: C5_SHELL_BUILD (shell\\build.cmd: test_host.exe, shell_composite.dll) and C5_RENDER_TEST_BUILD
(server/tests/render/build_render_tests.py: render_host_test.exe). The EXE is read for its terrain shaders only
(C5R_EXE, read-only, as test_terrain_scene.py). Every process runs on the session's hidden desktop (rule 14).
Negative cases check the outputs and the exit codes after the failure. Skipped counts as failed (conftest.py).
"""
import os
import re
import time
from pathlib import Path

import pytest

import shellrun
from test_lockstep import (EXACT, EXE_TAPS_OFF, HOST, INI, LsRun, check_bound, check_misses_justified, decode_shot,
                           qpc_freq, release_setting, uv_rows)

# Ticket 0627: the taps that read the real EXE are off on the stand-in (0490's EXE_TAPS_OFF, as in test_lockstep).
# This file's own synth tap stays on. Without it, water_tap (1 by default since 3608) refused the fake EXE with a
# DISABLED line, LsRun.finish() refused that line, and four cases here were red. 0490's run left out this file.
JR_TAPS = dict(EXE_TAPS_OFF, tap="1")

EXE = release_setting("C5R_EXE", "render_exe")   # ticket 4713: else the setting render_exe (tools/release/settings.py)
MAGENTA = (255, 0, 255)   # render_fallback=constant 0xFFFF00FF: the synthetic tap has no bake caches


@pytest.fixture
def rhost():
    d = os.environ.get("C5_RENDER_TEST_BUILD")
    p = Path(d) / "render_host_test.exe" if d else None
    if not p or not p.exists():
        pytest.fail("C5_RENDER_TEST_BUILD must name the bin folder of server/tests/render/build_render_tests.py")
    if not EXE.exists():
        pytest.fail("C5R_EXE %s missing (CivilizationV_DX11.exe, read-only)" % EXE)
    return p


@pytest.fixture
def jrun(tmp_path, hidden_desktop, built, request, host_closer):
    r = LsRun(tmp_path / "run", hidden_desktop, request.node.name[:24].replace("[", "_").replace("]", ""))
    # ticket 0632: LsRun ends its host through close_fn (conftest's host_closer, 0494), as test_lockstep's lrun sets it;
    # without it close_host raised "'NoneType' object is not callable" and the host's exit code stayed None
    r.close_fn = host_closer
    yield r
    r.stop()


def start_render_host(run, rhost, seconds=30, **keys):
    """render_host_test.exe with the server.cfg keys of a live run (render_handover=ring by default)."""
    cfg = {"render_section": run.section, "render_texture": run.texname, "render_exe": str(EXE),
           "render_log": str(run.dir / "render.log"), "render_handover": "ring", "render_fallback": "constant",
           "render_constant_rgba": "0xFFFF00FF"}
    cfg.update({k: str(v) for k, v in keys.items()})
    p = run.dir / "server.cfg"
    p.write_text("".join("%s=%s\n" % (k, v) for k, v in cfg.items() if v is not None), encoding="utf-8")
    out = run.dir / "render_host.out"
    proc = shellrun.launch([str(rhost), "--config", str(p), "--seconds", str(seconds), "--run-dir", str(run.dir),
                            "--game-dir", str(run.dir)], run.desktop, console=True, out_path=out)
    run.server = proc
    run.server_log = run.dir / "render.log"
    return proc, out


def render_summary(out: Path):
    s = out.read_text(errors="replace")
    m = re.search(r"result: render: (.*)", s)
    assert m, s[-2000:]
    d = {"text": m.group(1), "state": re.search(r"state (\w+)", m.group(1)).group(1)}
    for k, v in re.findall(r"([a-z ]+?) (-?\d+)(?:,|$| )", m.group(1)):
        d[k.strip()] = int(v)
    return d


def shot_image(run):
    """shellrun.Run.shot, then read the BMP; the DLL may still hold the file open (PermissionError seen 2026-11-13)."""
    p = run.shot()
    for _ in range(40):
        try:
            return shellrun.Image(p)
        except PermissionError:
            time.sleep(0.1)
    return shellrun.Image(p)


def wait_exit(proc, timeout=60):
    code = proc.wait(timeout)
    assert code is not None, "still running"
    return code


@pytest.mark.parametrize("sync", ["fence", "cpu"])
def test_server_ring_to_composite_ring(jrun, rhost, sync):
    """The joined path: the composite publishes at the fake UpdateViews and taps its synthetic field; the render host
    (event pacing) draws the terrain into ring slots; the composite takes them (lockstep, exact in most frames, every
    miss justified, every wait within deadline + 0.5 ms) and writes depth. On screen: the server's terrain (the constant
    fallback colour) where the field is in view."""
    jrun.set_ini(**dict(INI, **JR_TAPS, tap_test="synth", depth_write="1", handover="ring", ring_sync=sync))
    srv, out = start_render_host(jrun, rhost, seconds=30, render_sync=sync)
    jrun.start_host(*HOST, seconds=10)
    time.sleep(6.0)
    shot = shot_image(jrun)
    lines = jrun.finish()
    assert wait_exit(srv) == 0, out.read_text(errors="replace")[-2000:]
    rl = jrun.server_log.read_text(errors="replace")
    assert "pacing: the composite publishes: woken by <base>.Cam" in rl, rl[-2000:]
    assert re.search(r"terrain: state \S+ -> OK", rl), rl[-2000:]
    s = render_summary(out)
    assert s["state"] == "STOPPED" and "handover ring" in s["text"], s
    assert s["published"] > 300 and s["exceptions"] == 0 and s["draw errors"] == 0 and s["sink errors"] == 0, s
    # the composite: the ring source opened, frames drawn exactly from their own camera (machine load: a floor only)
    assert any("ring source open" in ln for ln in lines), lines[-20:]
    rows = uv_rows(jrun.rows())
    drawn = [x for x in rows if x["taken_f"] > 0]
    assert len(drawn) > 300, len(drawn)
    tail = drawn[-600:]
    # under ASan the cpu-sync server (GPU wait before READY, instrumented draw) misses every bound on a loaded machine
    # (2026-11-13: 0 of 600 exact, every miss justified): there only the rule is checked, as test_lockstep.py does
    exact = check_misses_justified(tail, share=0.0 if os.environ.get("C5_ASAN_LOG_DIR") else 0.5)
    assert all(x["slot"] >= 0 for x in exact)
    check_bound(rows, qpc_freq(jrun))
    assert sum(x["depth"] for x in tail) >= 0.5 * len(tail), "depth written in too few frames"   # also an older frame's
    ls = jrun.ls_lines()
    assert max(d.get("ring_gen", 0) for d in ls) >= 1
    if sync == "fence":
        assert sum(d.get("ring_fence_waits", 0) for d in ls) > 100, ls[-1]
    # the picture: the server's terrain in the composited back buffer
    frac = shellrun.fraction(shot.region(0, 0, shot.w, shot.h, step=8), lambda c: c == MAGENTA)
    assert frac > 0.05, frac


def test_server_ring_composite_v3_no_frames(jrun, rhost):
    """Negative: the server hands over by ring, the composite expects version 3 (handover=v3): no keyed-mutex texture
    ever appears, so the composite draws nothing from the server, logs the unknown frame ids, gives up waiting and goes
    on (host exit 0, no DISABLED); the server stays READY and stops in order (exit 0)."""
    jrun.set_ini(**dict(INI, **JR_TAPS, tap_test="synth", depth_write="1", handover="v3"))
    srv, out = start_render_host(jrun, rhost, seconds=20)
    jrun.start_host(*HOST, seconds=6)
    jrun.finish()
    assert wait_exit(srv) == 0
    rows = uv_rows(jrun.rows(), skip=0)
    assert rows and not any(x["kind"] == EXACT for x in rows), [x for x in rows if x["kind"] == EXACT][:2]
    assert sum(1 for x in rows if x["wait_off"]) > 0.5 * len(rows), "the composite did not give up waiting"
    s = render_summary(out)
    assert s["state"] == "STOPPED" and s["exceptions"] == 0, s


def test_server_ring_killed_composite_goes_on(jrun, rhost):
    """Negative: the render host is killed while the composite takes ring frames (fence sync): the composite keeps
    drawing the last frame or none, never waits past its bound, gives up waiting, and the host ends normally (exit 0)."""
    jrun.set_ini(**dict(INI, **JR_TAPS, tap_test="synth", depth_write="1", handover="ring", ring_sync="fence"))
    srv, out = start_render_host(jrun, rhost, seconds=30)
    jrun.start_host(*HOST, seconds=9)
    time.sleep(4.0)
    srv.kill()
    jrun.finish()
    rows = uv_rows(jrun.rows())
    exact_before = [x for x in rows if x["kind"] == EXACT]
    assert len(exact_before) > 100, len(exact_before)
    check_bound(rows, qpc_freq(jrun))
    tail = rows[-200:]
    assert tail and not any(x["kind"] == EXACT for x in tail), "exact frames after the server was killed"
    assert sum(1 for x in tail if x["wait_off"]) > 100, "no give-up after the server was killed"
