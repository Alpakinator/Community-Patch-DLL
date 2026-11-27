"""Issue 3308 (epic 3300): epic 3400's Residency inside server.exe (srvrender::ResidencySource behind the TileCacheSource
seam), the radius from the options file through SrvOptions(), the feed's changed plots into re-bakes, and the pacing
before the composite's first publish.

The same rig as test_render_host.py (render_host_test.exe = the render objects server.exe links; fake composite on a
hidden desktop). Every negative case checks the outputs (log lines, totals, the summary) and the exit code.
The pipeline case needs the player's install (C5R_GAME_DIR, read-only) and a tap with the material layer
(C5R_TINY_TAP: 3401's Tiny tap); a missing input fails the test (a skip would count as a failure anyway, COMMON 9).
"""
import importlib.util
import os
import re
import time
from pathlib import Path

import numpy as np
import pytest

import renderfx as fx


def release_setting(env, key):
    """Ticket 4716 (D174), as conftest.py's release_setting: `env` if set, else the path setting `key` of
    tools/release/settings.py; with neither, a path that does not exist (the test fails naming `env`)."""
    if env in os.environ:
        return Path(os.environ[env])
    spec = importlib.util.spec_from_file_location("c5_release_settings",
                                                  Path(__file__).resolve().parents[3] / "tools" / "release" / "settings.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    v = mod.get(key)
    return Path(v) if v is not None else Path("<%s not set: setting %s of tools/release/settings.py>" % (env, key))


GAME_DIR = release_setting("C5R_GAME_DIR", "render_game_dir")
TINY_TAP = Path(os.environ.get("C5R_TINY_TAP", "D:/c5-64bit-work/3400/ref/C5H-3401-TINY_0008/armA/heightfield.c5hf"))


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


def cameras(rig, first, last, hz=40.0, **kw):
    for f in range(first, last + 1):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7), **kw))
        time.sleep(1.0 / hz)


def res_lines(text):
    """The residency part of the per-second terrain lines, as dicts."""
    out = []
    for ln in text.splitlines():
        m = re.search(r"\| residency (land=.*?) \|", ln)
        if m:
            out.append({k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", m.group(1))})
    return out


def res_totals(text):
    m = re.search(r"residency totals: (.*)", text)
    assert m, text[-1500:]
    return {k.strip(): int(v) for k, v in re.findall(r"([a-z ]+?) (-?\d+)", m.group(1))}


def options_file(tmp, radius):
    p = Path(tmp) / "VP64Options.ini"
    p.write_text("[Renderer]\nBakedTileCameraRadius = %s\n" % radius, encoding="utf-8")
    return p


# ---------------------------------------------------------------------------------------------------------------
# the server's bake with the stand-in baker

@pytest.mark.parametrize("near", ["default", "0"])
def test_bake_near_key_default_on_and_off(rig, near):
    """Issue 0371: render_bake_near is 1 by default (the near pairs, terrain-bake-passes.md 3.5.4) and 0 turns them off;
    either way the stand-in's map is baked and drawn as before. The synthetic map is all height class 2, which the EXE's
    visible list never holds (class 3 only, terrain::exeTileClass3), so no tile takes a pair here: the 'near 1s:' line says
    on=1 / on=0 with nothing held, the camera frames counted (moving: the synthetic camera pans every frame)."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    kw = {} if near == "default" else {"render_bake_near": near}
    p = rig.start_until(render_tiles="bake", render_baker="standin", **kw)   # ticket 0496: was a fixed 5 s
    rig.wait_log(r"residency: bake-ahead complete", p=p)
    cameras(rig, 2, 80, hz=40)
    rig.wait_next_residency(p)   # a per-second line (and its 'near 1s:' line) after the cameras
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    on = near == "default"
    if on:
        assert "residency: near pairs on (render_bake_near=1, issue 0371)" in t and "state-5 bake by the stand-in" in t, t[:4000]
    else:
        assert "residency: near pairs off (render_bake_near=0)" in t, t[:4000]
    near_lines = [dict(re.findall(r"(\w+)=(-?[\d.]+)", ln.split("near 1s:", 1)[1])) for ln in t.splitlines() if "near 1s:" in ln]
    assert near_lines, t[-2000:]
    last = {k: float(v) for k, v in near_lines[-1].items()}
    assert last["on"] == (1 if on else 0) and last["held256"] == 0 and last["held512"] == 0 and last["tex"] == 0, last
    assert last["cam_moving_frames"] + last["cam_settled_frames"] > 0, last
    rt = res_totals(t)
    assert rt["bakes"] == 36 and rt["failed"] == 0 and rt["source failures"] == 0, rt


def test_bake_standin_whole_map_resident_and_drawn(rig):
    """render_tiles=bake (the default) with the stand-in baker: the residency bakes every land tile of the field (the
    synthetic 6 x 6 map is all class 2), the bake-ahead completes, no gray frame after it, and the frames show the
    stand-in's colours, not the fallback (constant magenta)."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until(render_tiles="bake", render_baker="standin", render_fallback="constant",   # 0496: was 6 s
                        render_constant_rgba="0xFFFF00FF", render_test_dump=str(rig.dump), render_test_dump_after=60)
    rig.wait_log(r"residency: bake-ahead complete", p=p)
    cameras(rig, 2, 120, hz=40)
    rig.wait_log(r"test: dump of frame \d+ to .* (written|FAILED)", p=p)
    rig.wait_next_residency(p, pred=fx.ahead_done)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "tiles from the server's bake (baker standin" in t and "BakedTileCameraRadius -1 ([Renderer] option)" in t, t[:3000]
    assert "residency: baker stand-in" in t, t[:3000]
    lines = res_lines(t)
    assert lines, t[-2000:]
    last = lines[-1]
    assert last["land"] == 36 and last["resident"] == 36 and last["pending"] == 0 and last["ahead_done"] == 1, last
    assert last["radius"] == -1 and last["failed"] == 0, last
    rt = res_totals(t)
    assert rt["bakes"] == 36 and rt["gray after ahead"] == 0 and rt["failed"] == 0 and rt["source failures"] == 0, rt
    # the dumped frame: the terrain drawn, and no fallback magenta where the caches are
    a = fx.image(rig.dump)
    drawn = a[..., :3].max(axis=2) > 0
    assert drawn.mean() > 0.3, drawn.mean()
    magenta = (a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 255)
    assert magenta[drawn].mean() < 0.01, magenta[drawn].mean()
    s, line = fx.summary(rig.stdout())
    assert s["state"] == "STOPPED" and s["exceptions"] == 0, line


def test_radius_from_the_options_file_and_its_override(rig, tmp_path):
    """BakedTileCameraRadius comes from the options file the server read (SrvOptions()); server.cfg render_bake_radius
    overrides it; both are logged with their source and reach the residency (radius= in its line)."""
    opt = options_file(tmp_path, 0)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    # ticket 0496: each run ends at its first residency line (was a fixed 3 s, which under ASan on a loaded machine ended
    # before the first per-second line)
    p = rig.start_until(options_file=opt, render_tiles="bake", render_baker="standin")
    rig.wait_residency(lambda d: True, p=p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "BakedTileCameraRadius 0 ([Renderer] option)" in t, t[:3000]
    assert res_lines(t) and res_lines(t)[-1]["radius"] == 0, res_lines(t)[-1:]
    # the override
    p = rig.start_until(options_file=opt, out=rig.tmp / "o2.out", render_tiles="bake", render_baker="standin",
                        render_bake_radius=3, render_log=str(rig.tmp / "r2.log"))
    rig.wait_residency(lambda d: True, p=p, log=rig.tmp / "r2.log")
    assert rig.stop(p) == 0
    t2 = (rig.tmp / "r2.log").read_text(errors="replace")
    assert "BakedTileCameraRadius 3 (server.cfg render_bake_radius)" in t2, t2[:3000]
    assert res_lines(t2)[-1]["radius"] == 3


def test_options_file_bad_value_gives_the_default(rig, tmp_path):
    """Negative: a bad value in the options file is the loader's warning and the default -1 (the server never stops for
    the options file)."""
    opt = options_file(tmp_path, "abc")
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until(options_file=opt, render_tiles="bake", render_baker="standin")   # 0496: was a fixed 2 s
    rig.wait_log(r"BakedTileCameraRadius .*\(\[Renderer\] option\)", p=p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    assert "BakedTileCameraRadius" in rig.stdout() and "warnings" in rig.stdout()
    assert "BakedTileCameraRadius -1 ([Renderer] option)" in rig.text(), rig.text()[:2000]


@pytest.mark.parametrize("field", ["improvement", "terrain"])
def test_feed_changed_plots_rebake_their_tiles(rig, field):
    """The feed's changed plots reach Residency::onPlotsChanged (index y * width + x of the game's map, the field's rows
    without the 2 + 2 margin rows): the tiles around them are raised and re-baked. Round 1 (the first layout / the first
    getter read of each plot after the table) is the state the field's bake stands for and is filtered; round 2 (a real
    change of an improvement, of a terrain type) passes. The synthetic map is 6 plots wide and 2 rows high without its
    margins: plots 0..11 lie on the map, 12..19 are counted and ignored by the residency."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    # ticket 0496: the run ends once the residency has taken both rounds and re-baked what they raised (was a fixed 6 s)
    p = rig.start_until(feed_plots=20, feed_delay_ms=1500, render_tiles="bake", render_baker="standin",
                        extra=["--feed-rounds", "2", "--feed-field", field])
    rig.wait_residency(lambda d: d["feed_plots"] >= 40 and d["rebakes"] > 0 and d["pending"] == 0, p=p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    assert "plots patched 40" in rig.stdout(), rig.stdout()[-1500:]
    last = res_lines(rig.text())[-1]
    assert last["feed_plots"] == 40 and last["feed_passed"] == 20 and last["feed_filtered"] == 20, last
    assert last["plots_changed"] == 12, last          # what Residency::onPlotsChanged took (the 12 plots on the map)
    assert last["raises"] > 0 and last["rebakes"] > 0, last
    tot = fx.totals(rig.text())
    assert tot["source plots changed"] == 40 and tot["lost plots changed"] == 0, tot


@pytest.mark.parametrize("field,rounds", [("fow", 2), ("owner", 2), ("improvement", 1)])
def test_feed_changes_the_bake_ignores(rig, field, rounds):
    """Negative (live run r1: ~700 re-bakes per AI turn, nearly all from fog and owner changes): fog and owner changes, and
    a plot's first layout, reach the source but not the residency: nothing raised, nothing re-baked."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    # ticket 0496: the run ends one per-second line after the residency took every round (was a fixed 5 s, which under
    # ASan on a loaded machine ended before the second round reached it); the line after shows nothing raised late
    p = rig.start_until(feed_plots=20, feed_delay_ms=1500, render_tiles="bake", render_baker="standin",
                        extra=["--feed-rounds", str(rounds), "--feed-field", field])
    i, _ = rig.wait_residency(lambda d: d["feed_plots"] >= 20 * rounds, p=p)
    rig.wait_residency(lambda d: True, p=p, after=i + 1)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    assert "plots patched %d" % (20 * rounds) in rig.stdout(), rig.stdout()[-1500:]
    last = res_lines(rig.text())[-1]
    assert last["feed_plots"] == 20 * rounds and last["feed_passed"] == 0 and last["feed_filtered"] == 20 * rounds, last
    assert last["plots_changed"] == 0 and last["raises"] == 0 and last["rebakes"] == 0, last


def test_failed_tile_stays_plain_and_is_counted(rig):
    """Negative: a tile the baker cannot bake (render_test_bake_fail, the stand-in's fault seam) is logged, counted and
    left plain; every other tile is baked; the renderer goes on."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until(render_tiles="bake", render_baker="standin", render_test_bake_fail="2,2")   # 0496: was 4 s
    i, _ = rig.wait_residency(lambda d: d["failed"] >= 1 and d["bakes"] >= 35, p=p)
    rig.wait_residency(lambda d: True, p=p, after=i + 1)   # one line more: nothing baked or failed after it
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert re.search(r"residency: bake of tile \(2, 2\) generation \d+ failed", t), t[-3000:]
    rt = res_totals(t)
    assert rt["failed"] == 1 and rt["bakes"] == 35, rt
    s, line = fx.summary(rig.stdout())
    assert s["state"] == "STOPPED" and s["exceptions"] == 0, line


def test_pipeline_without_an_install_draws_with_the_fallback(rig, tmp_path):
    """Negative: the pipeline baker cannot start (the install has no Assets/): one LOUD line, the source is not used, the
    terrain is still drawn (fallback colour), the process goes on (exit 0) and the totals count the failure."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    p = rig.start_until(render_tiles="bake", render_bake_install=str(tmp_path / "no-install"))   # 0496: was 3 s
    rig.wait_log(r"tap load seq 1 read", p=p)
    cameras(rig, 2, 40)
    rig.wait_next_second(p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert len(re.findall(r"LOUD: residency: not started \(baker pipeline", t)) == 1, t[-3000:]
    assert fx.totals(t)["drawn"] >= 10
    assert res_totals(t)["source failures"] == 1
    s, line = fx.summary(rig.stdout())
    assert s["state"] == "STOPPED", line


def test_tiles_none_and_tap_switch(rig):
    """render_tiles=none: no source, the fallback; render_tiles=tap: the EXE-cache tap (the test reference) - no
    residency line in either."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    # ticket 0496: each run ends one per-second line after its source line (was a fixed 2 s)
    p = rig.start_until(render_tiles="none")
    rig.wait_log(r"render_tiles=none", p=p)
    rig.wait_next_second(p)
    assert rig.stop(p) == 0
    t = rig.text()
    assert "render_tiles=none" in t and "residency" not in t.replace("residency=off", ""), t[-2000:]
    log2 = rig.tmp / "t2.log"
    p = rig.start_until(render_tiles="tap", out=rig.tmp / "t2.out", render_log=str(log2))
    rig.wait_log(r"tiles from the EXE's caches \(bake tap", p=p, log=log2)
    rig.wait_log(r"host 1s:", p=p, log=log2)
    assert rig.stop(p) == 0
    t2 = (rig.tmp / "t2.log").read_text(errors="replace")
    assert "tiles from the EXE's caches (bake tap" in t2 and "residency:" not in t2, t2[-2000:]


@pytest.mark.parametrize("key,value", [("render_tiles", "exe"), ("render_baker", "real"), ("render_bake_radius", "129"),
                                       ("render_bake_radius", "-2"), ("render_bake_radius", "abc"),
                                       ("render_bake_budget_ms", "0"), ("render_bake_budget_ms", "-1"),
                                       ("render_bake_rate", "0"), ("render_bake_burst", "-1"),
                                       ("render_bake_gpu_ms_per_s", "-0.5"), ("render_idle_poll_ms", "-1"),
                                       ("render_idle_poll_ms", "x"), ("render_test_bake_fail", "3"),
                                       ("render_test_bake_fail", "a,b"), ("render_test_bake_fail", "-1,2"),
                                       ("render_bake_warm", "2"), ("render_bake_warm", "on"),
                                       ("render_bake_near", "2"), ("render_bake_near", "on")])
def test_bake_config_refused(rig, key, value):
    """Negative: a malformed or out-of-range key of 3308 refuses the start with its key and value (exit 2), nothing is
    written into the section."""
    p = rig.start(seconds=1, **{key: value})
    assert rig.finish(p) == 2
    assert re.search(r"result: config refused: %s=%s: " % (re.escape(key), re.escape(value)), rig.stdout()), rig.stdout()
    assert rig.frame.server()["magic"] == 0


# ---------------------------------------------------------------------------------------------------------------
# the real baker (issue 3404's pipeline) on the player's install

@pytest.mark.parametrize("warm", ["default", "1", "loader0", "pages0"])
def test_pipeline_bakes_a_real_tap(rig, warm, monkeypatch):
    """The product's baker: 3401's Tiny tap (heights and material layer of a live load) and the player's install
    (read-only): the bake-ahead from the map's centre (no camera) completes, every land tile baked, none failed.
    Issue 0417: render_bake_warm=0 (the default) has no warm-up; render_bake_warm=1 (the test arm): the pipeline warms up
    at init (one report line, no error) and the map's bakes load no layer texture and no style (their first-use lines name
    height pages only).
    Ticket 0698: by default (C5_RENDER_LAYER_LOADER=1) the pipeline's loader reads the styles from init and the layer
    textures from the field's setMap, so no bake loads a layer texture or a style either (its lines: started, map set,
    queue empty); "loader0" (C5_RENDER_LAYER_LOADER=0, the behaviour before 0698) keeps 0417's check: the "pipeline:
    first use:" lines show the texture and style loads inside the bakes.
    Ticket 0714: by default (C5_RENDER_PAGE_WARM=1) the field builds every land tile's height page before the first
    bake (one line with the count and the cost), so no bake builds a page: no "(built)" in any first-use line; "pages0"
    (C5_RENDER_PAGE_WARM=0, the behaviour before 0714) has the page builds in the bakes again. The "1" arm (0417's
    warm-up) runs with C5_RENDER_PAGE_WARM=0, so that its check (first uses = page builds only) still has lines."""
    monkeypatch.setenv("C5_RENDER_LAYER_LOADER", "0" if warm == "loader0" else "1")
    monkeypatch.setenv("C5_RENDER_PAGE_WARM", "0" if warm in ("1", "pages0") else "1")
    if not GAME_DIR.joinpath("Assets").exists():
        pytest.fail("C5R_GAME_DIR %s has no Assets/ (the install, read-only)" % GAME_DIR)
    if not TINY_TAP.exists():
        pytest.fail("C5R_TINY_TAP %s missing (3401's Tiny tap with the material layer)" % TINY_TAP)
    payload = TINY_TAP.read_bytes()
    w, h = np.frombuffer(payload[8:16], dtype="<u4")
    rig.tap(load_seq=1, payload=payload, map_w=int(w), map_h=int(h))
    secs = 120 if os.environ.get("C5_ASAN_LOG_DIR") else 45   # the ASan build bakes several times slower
    kw = {"render_bake_warm": warm} if warm == "1" else {}
    # ticket 0496: the run ends one residency line after the bake-ahead (it ran the whole secs, its bound now)
    p = rig.start_until(bound=secs, game_dir=GAME_DIR, render_tiles="bake", **kw)
    m = rig.wait_log(r"residency: bake-ahead complete: (\d+) tiles resident", timeout=secs - 1, p=p)
    rig.wait_next_residency(p, pred=fx.ahead_done)
    assert rig.stop(p, timeout=45) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "residency: baker pipeline" in t, t[:3000]
    last = res_lines(t)[-1]
    assert int(m.group(1)) == last["land"] > 1000 and last["failed"] == 0 and last["pending"] == 0, last
    assert res_totals(t)["source failures"] == 0
    first = re.findall(r"pipeline: first use: .*", t)
    loads = [ln for ln in first if not re.search(r"textures [\d.]+ \(0 loaded\)", ln) or "(loaded)" in ln]
    # ticket 0714: the page warm-up's init line, its field line, and the page builds in the bakes
    pw = re.findall(r"residency: height pages of load seq 1 built at the field \(ticket 0714\): (\d+) pages for (\d+) land "
                    r"tiles, (\d+) built, in ([\d.]+) ms", t)
    built_in_bakes = [ln for ln in first if "(built)" in ln]
    if warm in ("1", "pages0"):
        assert "residency: height pages: built by their first bake (C5_RENDER_PAGE_WARM=0, before ticket 0714)" in t, t[:4000]
        assert not pw, pw
        assert built_in_bakes, "\n".join(first[:10])
    else:
        assert "residency: height pages: built at the field (ticket 0714, C5_RENDER_PAGE_WARM=1)" in t, t[:4000]
        assert len(pw) == 1, t[:6000]
        pages, tiles, built = int(pw[0][0]), int(pw[0][1]), int(pw[0][2])
        assert pages >= 1 and built == pages and tiles == last["land"], pw
        assert not built_in_bakes, "\n".join(built_in_bakes[:10])
        assert "LOUD: residency: height pages" not in t, t[:6000]
    if warm == "pages0":
        assert "pipeline: warm-up" not in t, t[:4000]
        assert first and not loads, "\n".join(first[:10])
        assert all("(built)" in ln or "; loader waits " in ln for ln in first), "\n".join(first[:10])
    elif warm == "1":
        assert re.search(r"pipeline: warm-up at init done in [\d.]+ ms: \d{2,} layer textures .* 11 throwaway bakes", t), t[:4000]
        assert "first failure" not in t, t[:4000]
        assert first and not loads, "\n".join(first[:10])   # page builds only
        assert all("(built)" in ln for ln in first), "\n".join(first[:10])
    elif warm == "default":
        # ticket 0698: the loader's lines, and no layer texture or style loaded by a bake (a first use is a page build or
        # a wait for the loader)
        assert "pipeline: warm-up" not in t, t[:4000]
        assert "read off the render thread by the pipeline's loader (ticket 0698, C5_RENDER_LAYER_LOADER=1)" in t, t[:4000]
        assert re.search(r"pipeline: loader: started \(free-threaded device", t), t[:4000]
        assert re.search(r"pipeline: loader: map set: \d{2,} layer texture names queued", t), t[:4000]
        # one "queue empty" line per batch: the styles' (taken at setMap), then the layer textures'; the counters add up
        m2 = re.findall(r"pipeline: loader: queue empty [\d.]+ ms after its first request: (\d+) layer textures \((\d+) "
                        r"missing\), 5 styles, .*?, 0 failed", t)
        assert m2 and int(m2[-1][0]) + int(m2[-1][1]) >= 10, t[:6000]
        # ticket 0714: with the pages built at the field the only first use a bake can still make is a wait for the
        # loader (often none at all)
        assert not loads, "\n".join(first[:10])
        assert all("; loader waits " in ln for ln in first), "\n".join(first[:10])
    else:
        # C5_RENDER_LAYER_LOADER=0: the bakes read as before 0698 (0417's check)
        assert "pipeline: warm-up" not in t, t[:4000]
        assert "read by the bakes on the render thread (C5_RENDER_LAYER_LOADER=0, before ticket 0698)" in t, t[:4000]
        assert "pipeline: loader:" not in t, t[:4000]
        assert loads, "\n".join(first[:10])


def _page_warm_frame(tmp_path, hidden_desktop, built, exe, mode):
    """Ticket 0714: one server run on 3401's Tiny tap with C5_RENDER_PAGE_WARM=mode, the camera on a land spot near the
    map's centre after the bake-ahead; returns (sha256 of the 20th published frame, render.log text)."""
    import hashlib
    import struct
    sub = tmp_path / ("pw%d" % mode)
    sub.mkdir()
    r = fx.Rig(sub, hidden_desktop, built, exe, "pw0714_%d" % mode)
    try:
        payload = TINY_TAP.read_bytes()
        w, h, X, Y = (int(x) for x in np.frombuffer(payload[8:24], dtype="<u4"))
        sx, sy, sz, ox, oy, water_z = struct.unpack_from("<6f", payload, 28)
        r.tap(load_seq=1, payload=payload, map_w=w, map_h=h)
        z = np.frombuffer(payload[128:128 + X * Y * 2], dtype="<u2").reshape(Y, X).astype(np.float64) * sz / 255.0
        land = z > water_z + 0.5
        ys, xs = np.nonzero(land[2:-2:16, 2:-2:16])
        ys, xs = ys * 16 + 2, xs * 16 + 2
        k = int(np.argmin((xs - X / 2) ** 2 + (ys - Y / 2) ** 2))
        cx, cy = int(xs[k]), int(ys[k])
        wx, wy, wz = cx * sx - ox, cy * sy - oy, float(z[cy, cx])
        view = fx.look_at_rh((wx, wy - 120, wz + 160), (wx, wy, wz), (0, 0, 1))

        def camera(f):
            c = fx.synth_camera(frame=f, samples=(X, Y))
            c.update({"view": list(view.ravel()) + [0.0] * 48, "camWorld": list(np.linalg.inv(view).ravel()), "ox": ox,
                      "oy": oy, "sx": sx, "sy": sy, "heightScale": sz})
            return c

        dump = sub / "frame.bin"
        secs = 240 if os.environ.get("C5_ASAN_LOG_DIR") else 120
        p = r.start_until(bound=secs, game_dir=GAME_DIR, render_tiles="bake", render_decals=0, render_fow=0,
                          render_overlay=0, render_ice=0, render_shadow=0, render_test_dump=str(dump),
                          render_test_dump_after=20)
        r.wait_log(r"residency: bake-ahead complete", timeout=secs - 5, p=p)
        r.wait_residency(fx.ahead_done, timeout=secs - 5, p=p)
        for f in range(1, 41):   # the same camera in both arms; the dump is the 20th published frame
            r.sec.write_camera(camera(f))
            time.sleep(0.025)
        r.wait_log(r"test: dump of frame \d+ to .* written", timeout=60, p=p)
        assert r.stop(p) == 0, r.stdout()[-2000:]
        return hashlib.sha256(dump.read_bytes()).hexdigest(), r.text()
    finally:
        r.close()


def test_page_warm_draws_the_same_frame(tmp_path, hidden_desktop, built, exe, monkeypatch):
    """Ticket 0714: the height pages built at the field are the bytes the bakes would have built, so the published frame
    is the same with C5_RENDER_PAGE_WARM=1 (the default) and 0 (SHA-256 of the 20th frame); with 1 no bake builds a page
    and the field's line names the pages, with 0 the bakes build them."""
    if not GAME_DIR.joinpath("Assets").exists():
        pytest.fail("C5R_GAME_DIR %s has no Assets/ (the install, read-only)" % GAME_DIR)
    if not TINY_TAP.exists():
        pytest.fail("C5R_TINY_TAP %s missing (3401's Tiny tap with the material layer)" % TINY_TAP)
    runs = {}
    for mode in (1, 0):
        monkeypatch.setenv("C5_RENDER_PAGE_WARM", str(mode))
        runs[mode] = _page_warm_frame(tmp_path, hidden_desktop, built, exe, mode)
    for mode, (hsh, text) in runs.items():
        print("0714 page warm %d: frame sha256 %s" % (mode, hsh))
        built_lines = [ln for ln in re.findall(r"pipeline: first use: .*", text) if "(built)" in ln]
        pw = re.search(r"residency: height pages of load seq 1 built at the field \(ticket 0714\): (\d+) pages for (\d+) "
                       r"land tiles, (\d+) built, in ([\d.]+) ms", text)
        if mode == 1:
            assert pw and int(pw.group(1)) >= 1 and pw.group(1) == pw.group(3), text[:6000]
            assert not built_lines, built_lines[:5]
        else:
            assert not pw and built_lines, text[:6000]
    assert runs[0][0] == runs[1][0], {m: r[0] for m, r in runs.items()}


# ---------------------------------------------------------------------------------------------------------------
# item 3: pacing before the first publish

def test_idle_pacing_before_a_camera_then_legacy_poll(rig):
    """Before the composite publishes and while there is nothing to draw (no camera, no field: the database phase and the
    load), the render thread wakes every render_idle_poll_ms (20) instead of polling the camera block every 0.5 ms; once
    a camera with a field is there (an old composite that publishes nothing) the 0.5 ms poll runs as before.

    Issue 0333: counted, not timed. Under MSVC ASan on a loaded workstation a fixed 2 s idle phase gave 10 and 19 idle
    waits (wanted >= 20) and the legacy phase fewer than 500 polls, so each phase now lasts until its count is reached
    (the run ends through --stop-event; --seconds is only the upper bound): the idle phase until a per-second line counts
    20 idle waits, every camera is written after the previous one was drawn, the legacy phase until 500 legacy waits.
    The loop ends each iteration in exactly one wait and counts it by kind, so the phases are checked wait by wait in the
    cumulative per-second lines: no legacy wait before the camera, no idle wait after the first legacy one. The periods
    are the ones the loop uses: the idle note prints render_idle_poll_ms, the legacy note the timer's constant."""
    stop, extra = rig.stop_event()
    p = rig.start(seconds=180, extra=extra)
    pre = rig.wait_second_line(lambda d: d["waits_idle"] >= 20 or d["waits_legacy"] > 0, timeout=60, p=p)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    rig.wait_log(r"tap load seq 1 read")
    for f in range(2, 61):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        t_end = time.time() + 20
        while rig.frame.server()["lastDrawnFrameId"] < f and time.time() < t_end and p.alive():
            time.sleep(0.002)
        assert rig.frame.server()["lastDrawnFrameId"] >= f, (f, rig.frame.server(), rig.text()[-2000:])
    rig.wait_second_line(lambda d: d["waits_legacy"] > 500 or d["waits_event"] > 0 or d["waits_fallback"] > 0, timeout=60, p=p)
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    t = rig.text()
    note_idle = t.find("pacing: no camera to draw and no publish yet: waking every 20.0 ms")
    note_legacy = t.find("pacing: legacy (the composite has never written the publish block): camera block polled every 0.5 ms")
    assert 0 <= note_idle < note_legacy, (note_idle, note_legacy, t[:3000])
    assert "no high-resolution waitable timer" not in t
    assert pre["waits_idle"] >= 20 and pre["waits_legacy"] == 0, pre   # before the camera existed: idle waits only
    s, line = fx.summary(rig.stdout())
    # idle waits only before the camera (20 ms each, up to ~47 ms with the default 15.6 ms timer resolution: ~20-50 a
    # second; the 0.5 ms poll would count ~1,700 in the first second of a plain build). Load only lowers this count.
    assert 20 <= s["idle"] <= 400, line
    assert s["legacy"] > 500 and s["waits event"] == 0 and s["pace fallbacks"] == 0, line
    sec = fx.second_lines(t)
    first_legacy = next(i for i, d in enumerate(sec) if d["waits_legacy"] > 0)
    assert all(d["waits_legacy"] == 0 for d in sec[:first_legacy]), sec
    assert all(d["waits_idle"] == sec[first_legacy]["waits_idle"] for d in sec[first_legacy:]), sec
    assert sec[-1]["waits_idle"] == s["idle"], (sec[-1], line)
    assert all(d["waits_event"] == 0 and d["waits_fallback"] == 0 for d in sec), sec
    assert fx.totals(t)["drawn"] >= 59   # cameras 2..60, each drawn before the next was written


def test_idle_pacing_off_keeps_the_old_poll(rig):
    """render_idle_poll_ms=0: the 0.5 ms poll throughout, as before 3308 (comparison and fallback).

    Issue 0333: counted, not timed. The check was "more than 500 legacy waits in a 2 s run"; under MSVC ASan on a loaded
    workstation that run counted 149-491. The loop ends each iteration in exactly one wait and counts it by kind (event,
    legacy, idle), so the run now lasts until 500 legacy waits are counted (--stop-event; --seconds only bounds it) and
    every wait of the run must be the legacy poll: no idle, event or fallback wait in the summary or in any per-second
    line, the legacy count rising between every two per-second lines (an iteration lies between two lines and ends in a
    wait). The poll's period is the one the loop uses: the note prints the timer's constant (kLegacyPollHns), and the
    no-timer fallback (1 ms sleeps) is not taken."""
    stop, extra = rig.stop_event()
    p = rig.start(seconds=120, render_idle_poll_ms=0, extra=extra)
    # ends early when a wait of another kind is counted (the checks below then name it)
    rig.wait_second_line(lambda d: d["waits_legacy"] > 500 or d["waits_idle"] > 0 or d["waits_event"] > 0 or d["waits_fallback"] > 0,
                         timeout=110, p=p)
    stop.set()
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert "result: --stop-event set after" in rig.stdout(), rig.stdout()[-2000:]
    s, line = fx.summary(rig.stdout())
    assert s["idle"] == 0 and s["waits event"] == 0 and s["legacy"] > 500 and s["pace fallbacks"] == 0, line
    t = rig.text()
    sec = fx.second_lines(t)
    assert sec and all(d["waits_idle"] == 0 and d["waits_event"] == 0 and d["waits_fallback"] == 0 for d in sec), sec
    legacy = [d["waits_legacy"] for d in sec]
    assert all(b > a for a, b in zip(legacy, legacy[1:])) and s["legacy"] > legacy[-1], legacy
    assert "pacing: legacy (the composite has never written the publish block): camera block polled every 0.5 ms" in t, t[:3000]
    assert "pacing: no camera to draw" not in t
    assert "no high-resolution waitable timer" not in t


def test_stop_event_that_does_not_exist_is_refused(rig):
    """Negative (issue 0333): render_host_test.exe --stop-event naming an event nobody created refuses the start (exit 2,
    the name and the error printed); the renderer never starts (the server block stays unwritten)."""
    name = "Local\\VPRenderTestStopMissing_%d" % os.getpid()
    p = rig.start(seconds=1, extra=["--stop-event", name])
    assert rig.finish(p) == 2
    out = rig.stdout()
    assert "--stop-event %s: cannot open (error 2)" % name in out, out
    assert rig.frame.server()["magic"] == 0


def test_idle_pacing_wakes_at_once_on_the_first_publish(rig):
    """While idle-paced the render thread also waits on <base>.Cam: the composite's first publish switches it to event
    pacing without waiting for the next 20 ms tick (here a 1 s idle tick shows it)."""
    cam = fx.Event(rig.section + ".Cam")
    rig.events.append(cam)
    p = rig.start_until(render_idle_poll_ms=1000)   # ticket 0496: was a fixed 4 s
    rig.wait_log(r"pacing: no camera to draw", p=p)
    time.sleep(0.5)
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.frame.publish(1)
    t0 = time.time()
    cam.set()
    rig.wait_log(r"pacing: the composite publishes", timeout=3)
    assert time.time() - t0 < 0.9
    assert rig.stop(p) == 0, rig.stdout()[-2000:]


# ---------------------------------------------------------------------------------------------------------------
# ticket 4555 (trees.md 15.4): the feed's table after the field, no plot changes

def terrain_counters(text):
    """Every key=value of the last 'terrain 1s:' line (the residency's art_fills, plots_changed, style_raises among
    them) as numbers."""
    lines = [ln for ln in text.splitlines() if "terrain 1s:" in ln]
    assert lines, text[-2000:]
    return {k: float(v) for k, v in re.findall(r"(\w+)=(-?[\d.]+)", lines[-1])}


def test_table_after_the_field_takes_the_styles_without_plot_changes(rig):
    """Ticket 4555 (trees.md 15.4, live on Tiny: the table came after the field and no plot changed, so every tile and
    every server tree stayed Generic all session). The field first (all Generic, 'none yet'), then the feed's table of the
    map's size (6 x 2 plots, every plot America) and no plot patch: the residency takes it at the next drain, the tiles are
    restyled once (America tiles, the raised tiles counted once and not again), plots_changed stays 0."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    rig.tap(load_seq=1)
    # ticket 0496: the run ends one per-second line after the second camera burst (was a fixed 6 s); mid is a line written
    # after the intake and the first burst
    p = rig.start_until(feed_plots=1, feed_delay_ms=1500, render_tiles="bake", render_baker="standin",
                        extra=["--feed-rounds", "0", "--feed-table", "6x2", "--feed-art", "1"])
    rig.wait_log(r"art types 6x2 from none yet \(all Generic\)", p=p)
    rig.wait_log(r"tile styles of load seq 1 after the field: art types 6x2 from the feed's table", p=p)
    cameras(rig, 2, 60, hz=40)
    rig.wait_next_residency(p)
    mid = terrain_counters(rig.text())
    cameras(rig, 61, 120, hz=40)
    rig.wait_next_residency(p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t, out = rig.text(), rig.stdout()
    assert "result: snapshot 6x2 art 1 taken" in out, out[-1500:]
    late = re.findall(r"tile styles of load seq 1 after the field: art types 6x2 from the feed's table \(table generation 1, 6x2\), "
                      r"tiles Generic (\d+) America (\d+) Asia (\d+) Africa (\d+) Europe (\d+)", t)
    assert len(late) == 1, t[-3000:]                                   # taken once
    g, am, asia, af, eu = (int(x) for x in late[0])
    assert am > 0 and asia == af == eu == 0 and g + am == 36, late
    last = terrain_counters(t)
    assert last["art_fills"] == 1 and last["plots_changed"] == 0, last
    assert last["style_raises"] > 0 and last["style_raises"] == mid["style_raises"], (mid.get("style_raises"), last)
    loud = [ln for ln in t.splitlines() if "LOUD" in ln and ("residency" in ln or "TileStyles" in ln)]
    assert not loud, loud[:5]   # (the rig's own LOUD line of the missing install's fog is not the residency's)


def test_table_before_the_field_unchanged(rig):
    """Ticket 4555, the order before it (the table about 4 s before the field, as on the huge save): the field takes the
    table itself ('from the feed's table' in its own line), no late intake ('after the field' never), nothing raised for
    a style."""
    rig.sec.write_camera(fx.synth_camera(frame=1))
    p = rig.start_until(feed_plots=1, render_tiles="bake", render_baker="standin",   # ticket 0496: was a fixed 6 s
                        extra=["--feed-rounds", "0", "--feed-table", "6x2", "--feed-art", "4"])
    rig.wait_stdout(r"result: snapshot 6x2 art 4 taken", p=p)
    rig.tap(load_seq=1)
    rig.wait_log(r"tile styles of load seq 1: art types 6x2 from the feed's table \(table generation 1\), tiles Generic \d+ "
                 r"America 0 Asia 0 Africa 0 Europe [1-9]", p=p)
    cameras(rig, 2, 100, hz=40)
    rig.wait_next_residency(p)
    assert rig.stop(p) == 0, rig.stdout()[-2000:]
    t = rig.text()
    assert "tile styles of load seq 1 after the field" not in t, t[-3000:]
    last = terrain_counters(t)
    assert last["art_fills"] == 1 and last["plots_changed"] == 0 and last["style_raises"] == 0, last
    loud = [ln for ln in t.splitlines() if "LOUD" in ln and ("residency" in ln or "TileStyles" in ln)]
    assert not loud, loud[:5]   # (the rig's own LOUD line of the missing install's fog is not the residency's)
